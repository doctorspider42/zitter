// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
//! Native assistant orchestration. Browser authority remains in the browser.
use serde_json::{json, Value};
use std::collections::{BTreeMap, VecDeque};
use std::sync::{Arc, Condvar, Mutex, OnceLock};
use std::time::{Duration, Instant};

#[cxx::bridge(namespace = "zitter::assistant")]
mod ffi {
    unsafe extern "C++" {
        include!("chromium/assistant/native_transport.h");
        fn transport_http(id: &str, request: &str) -> String;
        fn transport_cli(id: &str, request: &str) -> String;
        fn cancelled(id: &str) -> bool;
        fn request_key(input: &str) -> String;
        fn publish_progress(id: &str, text: &str);
    }
    extern "Rust" {
        fn runtime_request(path: &str, body: &str) -> String;
        fn runtime_stream(id: &str, kind: &str, chunk: &str);
        fn valid_model(model: &str) -> bool;
        fn decode_response(text: &str) -> String;
    }
}
const SYSTEM: &str = include_str!("system.txt");
const HELP: &str = include_str!("help.txt");
type Result<T> = std::result::Result<T, String>;
fn error(text: &str) -> String {
    text.to_owned()
}
fn text(v: &Value, key: &str) -> String {
    v[key].as_str().unwrap_or_default().to_owned()
}
fn bounded(s: &str, max: usize) -> String {
    s.chars().take(max).collect()
}
fn check(id: &str) -> Result<()> {
    if ffi::cancelled(id) {
        Err(error("Assistant task stopped."))
    } else {
        Ok(())
    }
}
fn valid_model(model: &str) -> bool {
    !model.is_empty()
        && model.len() <= 160
        && model.bytes().enumerate().all(|(i, c)| {
            c.is_ascii_alphanumeric() || c == b'_' || (i > 0 && b".:/@+-".contains(&c))
        })
}
fn decode(value: Value) -> Result<Value> {
    if !value["answer"].is_string() {
        return Err(error(
            "The model returned an invalid assistant response. Try another tool-capable model.",
        ));
    }
    let actions = value["actions"]
        .as_array()
        .ok_or_else(|| error("Invalid tool calls"))?;
    if actions.len() > 16 {
        return Err(error("Too many tool calls"));
    }
    let mut clean = Vec::new();
    for a in actions {
        let tool = a["tool"]
            .as_str()
            .ok_or_else(|| error("Invalid tool call"))?;
        let args: Value = serde_json::from_str(
            a["args"]
                .as_str()
                .ok_or_else(|| error("Invalid tool arguments"))?,
        )
        .map_err(|_| error("Invalid tool arguments"))?;
        if !args.is_object() {
            return Err(error("Invalid tool arguments"));
        }
        clean.push(json!({"tool": tool, "args": args}));
    }
    Ok(json!({"answer": value["answer"], "actions": clean}))
}
fn decode_response(raw: &str) -> String {
    match serde_json::from_str(raw)
        .map_err(|_| error("Invalid model JSON"))
        .and_then(decode)
    {
        Ok(v) => v.to_string(),
        Err(e) => json!({"error":e}).to_string(),
    }
}
fn schema() -> Value {
    json!({"type":"object","additionalProperties":false,"required":["answer","actions"],"properties":{
        "answer":{"type":"string"},"actions":{"type":"array","maxItems":16,"items":{
            "type":"object","additionalProperties":false,"required":["tool","args"],"properties":{
                "tool":{"type":"string"},"args":{"type":"string","description":"JSON encoded tool arguments"}}}}}})
}
// Decode only the answer string, including incomplete JSON and surrogate pairs.
fn partial_answer(raw: &str) -> String {
    let Some(start) = raw.find("\"answer\"") else {
        return String::new();
    };
    let tail = raw[start + 8..].trim_start();
    let Some(tail) = tail.strip_prefix(':') else {
        return String::new();
    };
    let Some(tail) = tail.trim_start().strip_prefix('"') else {
        return String::new();
    };
    let mut end = 0;
    let mut escaped = false;
    for (i, c) in tail.char_indices() {
        if c == '"' && !escaped {
            end = i;
            break;
        }
        if c == '\\' {
            escaped = !escaped;
        } else {
            escaped = false;
        }
        end = i + c.len_utf8();
    }
    // Only incomplete escapes at the tail can be repaired. Bound lookback so
    // malformed model output cannot cause quadratic rescanning of megabytes.
    for _ in 0..12 {
        if let Ok(s) = serde_json::from_str::<String>(&format!("\"{}\"", &tail[..end])) {
            return s;
        }
        let Some(last) = tail[..end].chars().next_back() else {
            break;
        };
        end -= last.len_utf8();
    }
    String::new()
}
#[derive(Default)]
struct Stream {
    pending: String,
    written: String,
}
fn streams() -> &'static Mutex<BTreeMap<String, Stream>> {
    static STREAMS: OnceLock<Mutex<BTreeMap<String, Stream>>> = OnceLock::new();
    STREAMS.get_or_init(Default::default)
}
fn runtime_stream(id: &str, kind: &str, chunk: &str) {
    if ffi::cancelled(id) {
        return;
    }
    let mut all = streams().lock().unwrap();
    let Some(s) = all.get_mut(id) else { return };
    s.pending.push_str(chunk);
    while let Some(newline) = s.pending.find('\n') {
        let line = s.pending[..newline].trim().to_owned();
        s.pending.drain(..newline + 1);
        let raw = if kind == "openai" {
            line.strip_prefix("data:").unwrap_or("").trim()
        } else {
            &line
        };
        let Ok(v) = serde_json::from_str::<Value>(raw) else {
            continue;
        };
        match kind {
            "openai" if v["type"] == "response.output_text.delta" => {
                s.written.push_str(v["delta"].as_str().unwrap_or_default())
            }
            "ollama" => s
                .written
                .push_str(v["message"]["content"].as_str().unwrap_or_default()),
            "codex"
                if matches!(v["type"].as_str(), Some("item.updated" | "item.completed"))
                    && v["item"]["type"] == "agent_message" =>
            {
                s.written = text(&v["item"], "text")
            }
            "claude" if v["type"] == "stream_event" => {
                let e = &v["event"];
                if e["type"] == "message_start" {
                    s.written.clear();
                }
                let d = &e["delta"];
                if d["type"] == "input_json_delta" {
                    s.written
                        .push_str(d["partial_json"].as_str().unwrap_or_default());
                }
                if d["type"] == "text_delta" {
                    s.written.push_str(d["text"].as_str().unwrap_or_default());
                }
            }
            _ => {}
        }
        ffi::publish_progress(id, &partial_answer(&s.written));
    }
}
struct Session {
    config: Value,
    permissions: Value,
    messages: Vec<Value>,
    steps: usize,
    date: Value,
    concurrency: usize,
    research: bool,
}
struct CacheEntry {
    until: Instant,
    value: Arc<(Mutex<Option<Result<Value>>>, Condvar)>,
}
#[derive(Default)]
struct State {
    sessions: VecDeque<(String, Arc<Mutex<Session>>)>,
    cache: BTreeMap<String, CacheEntry>,
    active: usize,
}
struct Engine {
    state: Mutex<State>,
    changed: Condvar,
}
fn engine() -> &'static Engine {
    static ENGINE: OnceLock<Engine> = OnceLock::new();
    ENGINE.get_or_init(|| Engine {
        state: Mutex::new(State::default()),
        changed: Condvar::new(),
    })
}
impl Engine {
    fn complete(&self, id: &str, s: &Session, messages: &[Value]) -> Result<Value> {
        check(id)?;
        // Permissions form part of the cache scope; helper termination clears it.
        let key = ffi::request_key(&json!([s.config, s.permissions, SYSTEM, messages]).to_string());
        let (entry, owner) = {
            let mut state = self.state.lock().unwrap();
            state.cache.retain(|_, e| e.until > Instant::now());
            if let Some(e) = state.cache.get(&key) {
                (e.value.clone(), false)
            } else {
                if state.cache.len() >= 128 {
                    if let Some(k) = state.cache.keys().next().cloned() {
                        state.cache.remove(&k);
                    }
                }
                let value = Arc::new((Mutex::new(None), Condvar::new()));
                state.cache.insert(
                    key.clone(),
                    CacheEntry {
                        until: Instant::now() + Duration::from_secs(300),
                        value: value.clone(),
                    },
                );
                (value, true)
            }
        };
        if !owner {
            let mut v = entry.0.lock().unwrap();
            while v.is_none() {
                check(id)?;
                v = entry
                    .1
                    .wait_timeout(v, Duration::from_millis(100))
                    .unwrap()
                    .0;
            }
            let result = v.clone().unwrap();
            drop(v);
            if result
                .as_ref()
                .is_err_and(|e| e == "Assistant task stopped.")
                && check(id).is_ok()
            {
                self.state.lock().unwrap().cache.remove(&key);
                return self.complete(id, s, messages);
            }
            return result;
        }
        let result = (|| {
            let mut state = self.state.lock().unwrap();
            while state.active >= 12 {
                check(id)?;
                state = self
                    .changed
                    .wait_timeout(state, Duration::from_millis(100))
                    .unwrap()
                    .0;
            }
            check(id)?;
            state.active += 1;
            drop(state);
            let result = provider(id, &s.config, messages, !s.research);
            let mut state = self.state.lock().unwrap();
            state.active -= 1;
            self.changed.notify_all();
            result
        })();
        *entry.0.lock().unwrap() = Some(result.clone());
        entry.1.notify_all();
        if result.is_err() {
            self.state.lock().unwrap().cache.remove(&key);
        }
        result
    }
    fn local(&self, id: &str, s: &Session, a: &Value, research: bool) -> Result<Option<Value>> {
        match a["tool"].as_str().unwrap_or_default() {
            "browser_help" => Ok(Some(Value::String(HELP.to_owned()))),
            "web_read" | "web_search" => {
                if s.permissions["web"] != true {
                    return Err(error("Web permission is disabled"));
                }
                Ok(Some(web(id, &text(a, "tool"), &a["args"])?))
            }
            _ if research => Err(error(
                "Research workers cannot access browser data or perform actions",
            )),
            _ => Ok(None),
        }
    }
    fn step(&self, id: &str, s: &mut Session) -> Result<Value> {
        for _ in 0..200 {
            s.steps += 1;
            if s.steps > 400 {
                return Err(error("Tool budget reached. Send a follow-up to continue."));
            }
            let result = self.complete(id, s, &s.messages)?;
            s.messages
                .push(json!({"role":"assistant", "content":result.to_string()}));
            let mut browser = Vec::new();
            let mut local = Vec::new();
            for a in result["actions"].as_array().unwrap() {
                match self.local(id, s, a, false) {
                    Ok(Some(v)) => local.push(json!({"tool":a["tool"],"result":v})),
                    Ok(None) => browser.push(a.clone()),
                    Err(e) => local.push(json!({"tool":a["tool"],"error":e})),
                }
            }
            if !local.is_empty() {
                s.messages.push(json!({"role":"user","content":json!({"untrusted_tool_results":local}).to_string()}));
            }
            if !browser.is_empty() || result["actions"].as_array().unwrap().is_empty() {
                return Ok(json!({"answer":result["answer"],"actions":browser}));
            }
        }
        Err(error("Tool budget reached. Send a follow-up to continue."))
    }
    fn research(&self, id: &str, s: &mut Session, prompt: &str) -> Result<Value> {
        if s.permissions["web"] != true {
            return Err(error("Enable web research for this session."));
        }
        let mut planning = s.messages.clone();
        planning.push(json!({"role":"user","content":"Plan deep research. Return 4–8 complementary web_search actions. Cover primary sources, independent reviews, user discussions, criticism and recent evidence. Do not answer yet."}));
        let plan = self.complete(id, s, &planning)?;
        let mut queries = Vec::new();
        for a in plan["actions"].as_array().unwrap() {
            let q = text(&a["args"], "query");
            if a["tool"] == "web_search"
                && !q.is_empty()
                && !queries.contains(&q)
                && queries.len() < 8
            {
                queries.push(q);
            }
        }
        if queries.is_empty() {
            queries.push(prompt.to_owned());
        }
        let reports = Mutex::new(Vec::new());
        let sources = Mutex::new(BTreeMap::new());
        for batch in queries.chunks(s.concurrency) {
            std::thread::scope(|scope| {
                for query in batch {
                    let reports = &reports;
                    let sources = &sources;
                    let session = &*s;
                    scope.spawn(move || {
                        let outcome = (|| -> Result<String> {
                            let mut messages = vec![json!({"role":"user","content":json!({"task":prompt,"research_angle":query,"date":session.date,"instruction":"Research this angle. Search, then read original sources and discussions. Cite URLs and explain evidence quality. Only research tools are available."}).to_string()})];
                            let search = web(id, "web_search", &json!({"query":query}))?;
                            let mut evidence = Vec::new();
                            for candidate in search["results"].as_array().into_iter().flatten().take(4) {
                                match web(id, "web_read", &json!({"url":candidate["url"]})) {
                                    Ok(page) => { sources.lock().unwrap().insert(text(&page,"url"),text(&page,"fetched_at")); evidence.push(page); }
                                    Err(_) => evidence.push(json!({"url":candidate["url"],"error":"Source could not be read"})),
                                }
                            }
                            messages.push(json!({"role":"user","content":json!({"untrusted_tool_results":[{"search":search,"evidence":evidence}]}).to_string()}));
                            for _ in 0..8 {
                                let result = self.complete(id, session, &messages)?;
                                messages.push(json!({"role":"assistant","content":result.to_string()}));
                                if result["actions"].as_array().unwrap().is_empty() { return Ok(text(&result,"answer")); }
                                let mut results = Vec::new();
                                for a in result["actions"].as_array().unwrap().iter().take(6) {
                                    match self.local(id, session, a, true) {
                                        Ok(Some(v)) => { if a["tool"] == "web_read" { sources.lock().unwrap().insert(text(&v,"url"),text(&v,"fetched_at")); } results.push(json!({"tool":a["tool"],"result":v})); }
                                        Ok(None) => unreachable!(),
                                        Err(e) => results.push(json!({"tool":a["tool"],"error":e})),
                                    }
                                }
                                messages.push(json!({"role":"user","content":json!({"untrusted_tool_results":results}).to_string()}));
                            }
                            messages.push(json!({"role":"user","content":"Budget exhausted. Synthesize evidence collected with citations. No more actions."}));
                            Ok(text(&self.complete(id, session, &messages)?, "answer"))
                        })();
                        reports.lock().unwrap().push(outcome.unwrap_or_else(|_| format!("Research angle {query} failed: source or provider unavailable")));
                    });
                }
            });
            check(id)?;
        }
        let mut messages = s.messages.clone();
        messages.push(json!({"role":"user","content":json!({"untrusted_research_reports":*reports.lock().unwrap(),"instruction":"Write a thorough final report. Reconcile findings, distinguish facts and opinions, cite sources inline, describe limitations and conflicting evidence. No actions."}).to_string()}));
        let mut answer = text(&self.complete(id, s, &messages)?, "answer");
        let sources = sources.into_inner().unwrap();
        if sources.is_empty() {
            answer.push_str("\n\nNo source pages could be read. Treat this report as unverified.");
        } else {
            answer.push_str("\n\nSources read during research:\n");
            for (url, date) in sources {
                answer.push_str(&format!("- {url} ({date})\n"));
            }
        }
        s.messages
            .push(json!({"role":"assistant","content":answer}));
        Ok(json!({"answer":answer,"actions":[]}))
    }
}
fn civil(days: i64) -> (i64, u32, u32) {
    let z = days + 719_468;
    let era = z.div_euclid(146_097);
    let doe = z - era * 146_097;
    let yoe = (doe - doe / 1460 + doe / 36_524 - doe / 146_096) / 365;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let day = (doy - (153 * mp + 2) / 5 + 1) as u32;
    let month = if mp < 10 { mp + 3 } else { mp - 9 } as u32;
    (yoe + era * 400 + i64::from(month <= 2), month, day)
}
fn local_date(date: &Value) -> Value {
    let Some(ms) = date["unix_ms"]
        .as_f64()
        .filter(|v| v.is_finite() && v.abs() < 8.64e15)
    else {
        return date.clone();
    };
    let offset = date["utc_offset_minutes"]
        .as_i64()
        .unwrap_or(0)
        .clamp(-1440, 1440);
    let minute = (ms as i64).div_euclid(60000) + offset;
    let (year, month, day) = civil(minute.div_euclid(1440));
    let hour = minute.rem_euclid(1440) / 60;
    json!({"local_time":format!("{year:04}-{month:02}-{day:02}T{hour:02}:{:02}", minute.rem_euclid(60)),"utc_offset_minutes":offset})
}

fn runtime_request(path: &str, body: &str) -> String {
    let run = || -> Result<Value> {
        let v: Value =
            serde_json::from_str(body).map_err(|_| error("Invalid assistant request"))?;
        let id = v["id"]
            .as_str()
            .filter(|s| !s.is_empty() && s.len() <= 100)
            .ok_or_else(|| error("Invalid session"))?;
        let engine = engine();
        if path == "/delete" {
            engine
                .state
                .lock()
                .unwrap()
                .sessions
                .retain(|(key, _)| key != id);
            return Ok(json!({}));
        }
        let session = {
            let mut state = engine.state.lock().unwrap();
            if let Some(pos) = state.sessions.iter().position(|(key, _)| key == id) {
                let entry = state.sessions.remove(pos).unwrap();
                let session = entry.1.clone();
                state.sessions.push_back(entry);
                session
            } else if path == "/turn" {
                let config = &v["config"];
                if !matches!(
                    config["provider"].as_str(),
                    Some("openai" | "ollama" | "claude" | "codex")
                ) || !valid_model(&text(config, "model"))
                {
                    return Err(error("Choose a valid backend and model ID"));
                }
                if state.sessions.len() >= 32 {
                    state.sessions.pop_front();
                }
                let mut messages = Vec::new();
                if let Some(history) = v["history"].as_array() {
                    for h in history.iter().skip(history.len().saturating_sub(30)) {
                        let t = bounded(&text(h, "text"), 8000);
                        if t.is_empty() {
                            continue;
                        }
                        let content = if h["role"] == "user" {
                            json!({"earlier_prompt":t})
                        } else if h["role"] == "assistant" {
                            json!({"answer":t,"actions":[]})
                        } else {
                            continue;
                        };
                        messages.push(json!({"role":h["role"],"content":content.to_string()}));
                    }
                }
                let session = Arc::new(Mutex::new(Session {
                    config: config.clone(),
                    permissions: v["permissions"].clone(),
                    messages,
                    steps: 0,
                    date: Value::Null,
                    concurrency: 12,
                    research: false,
                }));
                state.sessions.push_back((id.to_owned(), session.clone()));
                session
            } else {
                return Err(error("Session expired. Start a new session."));
            }
        };
        let mut s = session.lock().unwrap();
        check(id)?;
        if path == "/turn" {
            if s.config != v["config"] || s.permissions != v["permissions"] {
                return Err(error(
                    "Start a new session after changing model or permissions.",
                ));
            }
            let prompt = text(&v, "prompt");
            if prompt.trim().is_empty() || prompt.encode_utf16().count() > 32000 {
                return Err(error("Invalid session or message"));
            }
            if s.messages
                .iter()
                .map(|m| {
                    m["content"]
                        .as_str()
                        .unwrap_or_default()
                        .encode_utf16()
                        .count()
                })
                .sum::<usize>()
                > 600000
            {
                return Err(error("Session context is full. Start a new session."));
            }
            s.steps = 0;
            s.research = v["research"] == true;
            s.date = local_date(&v["date"]);
            s.concurrency = v["concurrency"].as_u64().unwrap_or(12).clamp(1, 12) as usize;
            let content = json!({"prompt":prompt,"date":s.date,"permissions":s.permissions,"background":v["background"] == true,"untrusted_browser_context":v["context"]});
            s.messages
                .push(json!({"role":"user","content":content.to_string()}));
            if v["research"] == true {
                return engine.research(id, &mut s, &prompt);
            }
        } else if path == "/continue" {
            let mut results = v["results"].as_array().cloned().unwrap_or_default();
            let mut images = Vec::new();
            for r in &mut results {
                if let Some(image) = r["result"]["image"].as_str().map(str::to_owned) {
                    if images.len() >= 2 || image.len() > 4_000_000 {
                        r["result"]["image"] = json!("omitted");
                    } else {
                        images.push(image);
                        r["result"]["image"] = json!(format!("attached image {}", images.len()));
                    }
                }
            }
            let mut message = json!({"role":"user","content":json!({"untrusted_tool_results":results}).to_string()});
            if !images.is_empty() {
                for old in &mut s.messages {
                    old.as_object_mut().unwrap().remove("images");
                }
                message["images"] = json!(images);
            }
            s.messages.push(message);
        } else {
            return Err(error("Unknown assistant operation"));
        }
        engine.step(id, &mut s)
    };
    match run() {
        Ok(value) => value.to_string(),
        Err(e) => json!({"error":e}).to_string(),
    }
}
fn provider(id: &str, config: &Value, messages: &[Value], progress: bool) -> Result<Value> {
    check(id)?;
    let kind = text(config, "provider");
    if progress {
        streams()
            .lock()
            .unwrap()
            .insert(id.to_owned(), Stream::default());
    }
    let result = provider_inner(id, config, messages, &kind);
    let result = check(id).and(result);
    if progress {
        streams().lock().unwrap().remove(id);
        ffi::publish_progress(id, "");
    }
    result
}
fn provider_http_body(config: &Value, messages: &[Value], kind: &str) -> Value {
    if kind == "openai" {
        let input: Vec<_> = messages.iter().map(|m| {
                let content = if let Some(images) = m["images"].as_array() {
                    let mut content = vec![json!({"type":"input_text","text":m["content"]})];
                    content.extend(images.iter().filter_map(Value::as_str).map(|i|json!({"type":"input_image","image_url":format!("data:image/jpeg;base64,{i}")}))); json!(content)
                } else { m["content"].clone() };
                json!({"role":m["role"],"content":content})
            }).collect();
        json!({"model":config["model"],"store":false,"instructions":SYSTEM,"stream":true,"input":input,"text":{"format":{"type":"json_schema","name":"zitter_turn","strict":true,"schema":schema()}},"prompt_cache_key":"zitter-assistant-v1"})
    } else {
        let mut input = vec![json!({"role":"system","content":SYSTEM})];
        input.extend(messages.iter().cloned());
        json!({"model":config["model"],"stream":true,"keep_alive":"10m","format":schema(),"messages":input})
    }
}

fn provider_inner(id: &str, config: &Value, messages: &[Value], kind: &str) -> Result<Value> {
    let reply: Value = if kind == "codex" || kind == "claude" {
        serde_json::from_str(&ffi::transport_cli(id, &json!({"provider":kind,"model":config["model"],"system":SYSTEM,"messages":messages,"schema":schema()}).to_string()))
    } else {
        let body = provider_http_body(config, messages, kind);
        serde_json::from_str(&ffi::transport_http(id, &json!({"provider":kind,"body":body}).to_string()))
    }.map_err(|_|error("Provider request failed. Check credentials, endpoint and model."))?;
    if let Some(e) = reply["error"].as_str() {
        return Err(bounded(e, 240));
    }
    check(id)?;
    decode_provider_body(kind, &text(&reply, "body"))
}
fn decode_provider_body(kind: &str, body: &str) -> Result<Value> {
    let mut written = String::new();
    let mut final_value = None;
    let mut refused = BTreeMap::new();
    let mut recovered = Vec::new();
    for line in body.lines() {
        let raw = if kind == "openai" {
            line.strip_prefix("data:").unwrap_or("").trim()
        } else {
            line.trim()
        };
        let Ok(v) = serde_json::from_str::<Value>(raw) else {
            continue;
        };
        match kind {
            "openai" => {
                if v["type"] == "response.output_text.delta" {
                    written.push_str(v["delta"].as_str().unwrap_or_default());
                }
                if v["type"] == "response.completed" {
                    for item in v["response"]["output"].as_array().into_iter().flatten() {
                        for c in item["content"].as_array().into_iter().flatten() {
                            if c["type"] == "output_text" {
                                written = text(c, "text");
                            }
                        }
                    }
                }
                if matches!(v["type"].as_str(), Some("response.failed" | "error")) {
                    return Err(error("OpenAI could not complete the request."));
                }
            }
            "ollama" => {
                if v.get("error").is_some() {
                    return Err(error("Ollama could not complete the request."));
                }
                written.push_str(v["message"]["content"].as_str().unwrap_or_default());
            }
            "codex" => {
                if matches!(v["type"].as_str(), Some("turn.failed" | "error")) {
                    return Err(error("Codex could not complete the request."));
                }
                if v["item"]["type"] == "agent_message" {
                    written = text(&v["item"], "text");
                }
            }
            "claude" => {
                for block in v["message"]["content"].as_array().into_iter().flatten() {
                    if block["type"] == "tool_use" {
                        refused.insert(text(block, "id"), block.clone());
                    }
                    if block["type"] == "tool_result"
                        && block["content"]
                            .to_string()
                            .contains("No such tool available")
                    {
                        if let Some(call) = refused.get(&text(block, "tool_use_id")) {
                            if recovered.len() < 16 {
                                recovered.push(
                                    json!({"tool":call["name"],"args":call["input"].to_string()}),
                                );
                            }
                        }
                    }
                }
                if v["type"] == "result" {
                    if v["is_error"] == true {
                        return Err(error("Claude could not complete the request."));
                    }
                    final_value = Some(if v["structured_output"].is_object() {
                        v["structured_output"].clone()
                    } else {
                        serde_json::from_str(v["result"].as_str().unwrap_or_default())
                            .map_err(|_| error("Invalid Claude response"))?
                    });
                }
            }
            _ => return Err(error("Unknown provider")),
        }
    }
    let mut value = final_value.unwrap_or(serde_json::from_str(&written).unwrap_or(Value::Null));
    if kind == "claude"
        && value["actions"].as_array().is_some_and(Vec::is_empty)
        && !recovered.is_empty()
    {
        value = json!({"answer":"","actions":recovered});
    }
    decode(value)
}
fn web(id: &str, tool: &str, args: &Value) -> Result<Value> {
    check(id)?;
    // URL parsing, address pinning, extraction and cache live in the native adapter.
    let raw = ffi::transport_http(
        id,
        &json!({"provider":"research","tool":tool,"args":args}).to_string(),
    );
    let value: Value = serde_json::from_str(&raw).map_err(|_| error("Research request failed"))?;
    if let Some(e) = value["error"].as_str() {
        return Err(bounded(e, 240));
    }
    Ok(value)
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn provider_requests_preserve_privacy_schema_and_image_inputs() {
        let config = json!({"model":"selected"});
        let messages = vec![json!({"role":"user","content":"page context","images":["aW1hZ2U="]})];
        let openai = provider_http_body(&config, &messages, "openai");
        assert_eq!(openai["store"], false);
        assert_eq!(openai["instructions"], SYSTEM);
        assert_eq!(openai["prompt_cache_key"], "zitter-assistant-v1");
        assert_eq!(openai["text"]["format"]["strict"], true);
        assert_eq!(openai["input"][0]["content"][0]["text"], "page context");
        assert_eq!(
            openai["input"][0]["content"][1]["image_url"],
            "data:image/jpeg;base64,aW1hZ2U="
        );
        let ollama = provider_http_body(&config, &messages, "ollama");
        assert_eq!(ollama["keep_alive"], "10m");
        assert_eq!(ollama["format"], schema());
        assert_eq!(ollama["messages"][1]["images"][0], "aW1hZ2U=");
    }
    #[test]
    fn research_workers_cannot_request_browser_tools() {
        let session = Session {
            config: Value::Null,
            permissions: json!({"web":false}),
            messages: vec![],
            steps: 0,
            date: Value::Null,
            concurrency: 2,
            research: true,
        };
        for tool in [
            "history",
            "read_page",
            "list_tabs",
            "click",
            "type",
            "schedule_task",
        ] {
            let result = engine().local("offline", &session, &json!({"tool":tool,"args":{}}), true);
            assert!(
                result.unwrap_err().contains("cannot access browser"),
                "{tool}"
            );
        }
        assert!(engine()
            .local(
                "offline",
                &session,
                &json!({"tool":"web_read","args":{}}),
                false
            )
            .unwrap_err()
            .contains("Web permission"));
    }
    #[test]
    fn local_time_preserves_offset_and_day_rollover() {
        assert_eq!(
            local_date(&json!({"unix_ms":0,"utc_offset_minutes":-60}))["local_time"],
            "1969-12-31T23:00"
        );
        assert_eq!(
            local_date(&json!({"unix_ms":0,"utc_offset_minutes":60}))["local_time"],
            "1970-01-01T01:00"
        );
    }
    #[test]
    fn all_provider_streams_decode_to_the_same_contract() {
        let response =
            json!({"answer":"Ready 😀","actions":[{"tool":"list_tabs","args":"{}"}]}).to_string();
        let expected = decode(serde_json::from_str(&response).unwrap()).unwrap();
        let fixtures = [
            (
                "openai",
                format!(
                    "data: {}\n\n",
                    json!({"type":"response.output_text.delta","delta":response})
                ),
            ),
            (
                "ollama",
                format!("{}\n", json!({"message":{"content":response}})),
            ),
            (
                "codex",
                format!(
                    "{}\n",
                    json!({"type":"item.completed","item":{"type":"agent_message","text":response}})
                ),
            ),
            (
                "claude",
                format!(
                    "{}\n",
                    json!({"type":"result","structured_output":serde_json::from_str::<Value>(&response).unwrap()})
                ),
            ),
        ];
        for (kind, body) in fixtures {
            assert_eq!(
                decode_provider_body(kind, &body).unwrap(),
                expected,
                "{kind}"
            );
        }
    }
    #[test]
    fn provider_errors_do_not_expose_diagnostics() {
        for (kind, body) in [
            (
                "openai",
                "data: {\"type\":\"error\",\"message\":\"secret credential\"}\n",
            ),
            ("ollama", "{\"error\":\"secret credential\"}\n"),
            (
                "codex",
                "{\"type\":\"turn.failed\",\"error\":\"secret credential\"}\n",
            ),
            (
                "claude",
                "{\"type\":\"result\",\"is_error\":true,\"result\":\"secret credential\"}\n",
            ),
        ] {
            let e = decode_provider_body(kind, body).unwrap_err();
            assert!(!e.contains("secret"), "{kind}");
        }
    }
    #[test]
    fn refused_claude_tools_recover_without_overriding_explicit_actions() {
        let events = [
            json!({"message":{"content":[{"type":"tool_use","id":"t","name":"read_page","input":{"tab":"a"}}]}}),
            json!({"message":{"content":[{"type":"tool_result","tool_use_id":"t","content":"No such tool available"}]}}),
            json!({"type":"result","structured_output":{"answer":"Unavailable","actions":[]}}),
        ];
        let body = events
            .iter()
            .map(Value::to_string)
            .collect::<Vec<_>>()
            .join("\n");
        assert_eq!(
            decode_provider_body("claude", &body).unwrap()["actions"][0],
            json!({"tool":"read_page","args":{"tab":"a"}})
        );
    }
    #[test]
    fn schema_rejects_invalid_arguments() {
        for raw in [
            "null",
            "{}",
            r#"{"answer":"","actions":[{"tool":"click","args":"null"}]}"#,
        ] {
            assert!(decode_response(raw).contains("error"));
        }
        assert_eq!(
            serde_json::from_str::<Value>(&decode_response(
                r#"{"answer":"ok","actions":[{"tool":"list_tabs","args":"{}"}]}"#
            ))
            .unwrap()["actions"][0]["args"],
            json!({})
        );
    }
    #[test]
    fn model_cannot_inject_options() {
        for m in ["", "--flag", "-p", "model\n--flag"] {
            assert!(!valid_model(m));
        }
        for m in ["gpt-5.4", "qwen3:8b", "vendor/model+tag", "sonnet"] {
            assert!(valid_model(m));
        }
    }
    #[test]
    fn partial_json_handles_unicode_and_truncation() {
        assert_eq!(partial_answer(r#"{"answer":"Czy"#), "Czy");
        assert_eq!(
            partial_answer(r#"{"answer":"a\nb \"c\" \u0105"#),
            "a\nb \"c\" ą"
        );
        assert_eq!(partial_answer(r#"{"answer":"x\"#), "x");
        assert_eq!(partial_answer(r#"{"answer":"\ud83d\ude00"#), "😀");
        assert_eq!(partial_answer(r#"{"actions":[]}"#), "");
    }
    #[test]
    fn invalid_partial_output_has_bounded_recovery() {
        let raw = format!("{{\"answer\":\"bad\\q{}", "x".repeat(1_000_000));
        assert!(partial_answer(&raw).is_empty());
    }
    #[test]
    fn instructions_keep_browser_contracts() {
        for tool in [
            "adblock_inspect {tab:string}",
            "adblock_add_rules {tab:string,rules:string[]",
            "adblock_diagnose {tab:string}",
            "ask_user",
            "schedule_task",
            "add_search_engine",
        ] {
            assert!(SYSTEM.contains(tool), "{tool}");
        }
        assert!(SYSTEM.contains("asks the user natively"));
        assert!(HELP.contains("Ad blocking is built in"));
    }
}
