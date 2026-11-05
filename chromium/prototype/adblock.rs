// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
//! Zitter's bridge to Brave's adblock-rust (MPL-2.0, vendored unmodified in
//! third_party/rust/adblock): parse filter lists, match requests, return
//! cosmetic selectors and check single rules the assistant proposes.
//! Every call runs on whichever thread owns the engine; nothing here blocks.
use adblock::lists::{parse_filter, FilterSet, ParseOptions, ParsedLine};
use adblock::request::Request;
use std::collections::HashSet;
use std::sync::atomic::{AtomicPtr, Ordering};

#[cxx::bridge(namespace = "zitter::adblock")]
mod ffi {
    // Hide and keep-visible selectors for one page. `procedural` holds rules
    // with an action (`:remove()`, `:style()`...) as adblock-rust's JSON; hide
    // selectors may carry procedural operators (`:has-text()`), which the page
    // script runs (content_blocking.js).
    struct CosmeticResources {
        hide_selectors: Vec<String>,
        procedural: Vec<String>,
        exceptions: Vec<String>,
        generichide: bool,
    }
    // One rule as the parser sees it. `kind`: 0 invalid, 1 network, 2 cosmetic,
    // 3 comment or empty line.
    struct RuleCheck {
        kind: u8,
        error: String,
    }
    extern "Rust" {
        type Engine;
        // Every list text (EasyList, EasyPrivacy, the profile's own rules) in one engine.
        fn new_engine(lists: &Vec<String>) -> Box<Engine>;
        fn rule_count(self: &Engine) -> usize;
        // 0 allow, 1 block, 2 a rule matched but an exception lets it through.
        fn check_request(self: &Engine, url: &str, source_url: &str, request_type: &str) -> u8;
        fn cosmetic_resources(self: &Engine, url: &str) -> CosmeticResources;
        fn hidden_class_id_selectors(self: &Engine, classes: &Vec<String>, ids: &Vec<String>, exceptions: &Vec<String>) -> Vec<String>;
        fn check_rule(rule: &str) -> RuleCheck;
        // Whether `rule` only acts on pages of `site` (an eTLD+1, like "example.com").
        fn rule_in_scope(rule: &str, site: &str) -> bool;
        fn count_rules(list: &str) -> usize;
    }
}

/// eTLD+1 resolution comes from Chromium's registry_controlled_domains through
/// a plain C function pointer the browser registers at startup. Without it
/// (Rust unit tests), the last two labels are the domain.
type DomainFn = unsafe extern "C" fn(host: *const u8, len: usize, start: *mut usize, end: *mut usize);
static DOMAIN_FN: AtomicPtr<()> = AtomicPtr::new(std::ptr::null_mut());

#[unsafe(no_mangle)]
pub extern "C" fn zitter_adblock_set_domain_resolver(resolver: DomainFn) {
    DOMAIN_FN.store(resolver as *mut (), Ordering::Release);
}

struct Resolver;
impl adblock::url_parser::ResolvesDomain for Resolver {
    fn get_host_domain(&self, host: &str) -> (usize, usize) {
        let pointer = DOMAIN_FN.load(Ordering::Acquire);
        if !pointer.is_null() {
            let resolve: DomainFn = unsafe { std::mem::transmute(pointer) };
            let (mut start, mut end) = (0usize, host.len());
            unsafe { resolve(host.as_ptr(), host.len(), &mut start, &mut end) };
            if start <= end && end <= host.len() && host.is_char_boundary(start) && host.is_char_boundary(end) {
                return (start, end);
            }
            return (0, host.len());
        }
        let mut dots = host.rmatch_indices('.');
        match (dots.next(), dots.next()) {
            (Some(_), Some((second, _))) => (second + 1, host.len()),
            _ => (0, host.len()),
        }
    }
}
fn ensure_resolver() {
    static ONCE: std::sync::Once = std::sync::Once::new();
    ONCE.call_once(|| { let _ = adblock::url_parser::set_domain_resolver(Box::new(Resolver)); });
}

pub struct Engine {
    engine: adblock::Engine,
    rules: usize,
}

fn new_engine(lists: &Vec<String>) -> Box<Engine> {
    ensure_resolver();
    let mut set = FilterSet::new(false);
    let mut rules = 0;
    for list in lists {
        rules += count_rules(list);
        set.add_filter_list(list.clone(), ParseOptions::default());
    }
    Box::new(Engine { engine: adblock::Engine::new_with_filter_set(set), rules })
}

impl Engine {
    fn rule_count(&self) -> usize { self.rules }
    fn check_request(&self, url: &str, source_url: &str, request_type: &str) -> u8 {
        let Ok(request) = Request::new(url, source_url, request_type, "GET") else { return 0 };
        let result = self.engine.check_network_request(&request);
        if result.should_block() { 1 } else if result.filter.is_some() || result.exception.is_some() { 2 } else { 0 }
    }
    fn cosmetic_resources(&self, url: &str) -> ffi::CosmeticResources {
        let resources = self.engine.url_cosmetic_resources(url);
        let mut hide: Vec<String> = resources.hide_selectors.into_iter().collect();
        hide.sort();
        let mut procedural: Vec<String> = resources.procedural_actions.into_iter().collect();
        procedural.sort();
        let mut exceptions: Vec<String> = resources.exceptions.into_iter().collect();
        exceptions.sort();
        ffi::CosmeticResources { hide_selectors: hide, procedural, exceptions, generichide: resources.generichide }
    }
    fn hidden_class_id_selectors(&self, classes: &Vec<String>, ids: &Vec<String>, exceptions: &Vec<String>) -> Vec<String> {
        let exceptions: HashSet<String> = exceptions.iter().cloned().collect();
        let mut selectors = self.engine.hidden_class_id_selectors(classes, ids, &exceptions);
        selectors.sort();
        selectors
    }
}

fn is_comment(line: &str) -> bool {
    let line = line.trim();
    line.is_empty() || line.starts_with('!') || (line.starts_with('[') && line.ends_with(']'))
}

fn check_rule(rule: &str) -> ffi::RuleCheck {
    ensure_resolver();
    if is_comment(rule) { return ffi::RuleCheck { kind: 3, error: String::new() }; }
    if rule.contains('\n') || rule.contains('\r') {
        return ffi::RuleCheck { kind: 0, error: "one rule per line".into() };
    }
    match parse_filter(rule, false, ParseOptions::default()) {
        Ok(ParsedLine::Network(_)) => ffi::RuleCheck { kind: 1, error: String::new() },
        Ok(ParsedLine::Cosmetic(filter)) => {
            // Scriptlets need uBO's resources, which Zitter does not ship (GPL):
            // they would parse and silently do nothing. Procedural operators and
            // actions run in Zitter's own page script, which checks them in full.
            if filter.mask.contains(adblock::filters::cosmetic::CosmeticFilterMask::SCRIPT_INJECT) || rule.contains("#+js(") {
                return ffi::RuleCheck { kind: 0, error: "scriptlets (+js) are not supported".into() };
            }
            if let Some(error) = selector_problem(&rule[rule.find('#').unwrap_or(0)..]) {
                return ffi::RuleCheck { kind: 0, error };
            }
            ffi::RuleCheck { kind: 2, error: String::new() }
        }
        Err(error) => ffi::RuleCheck { kind: 0, error: error.to_string() },
    }
}

// Operators content_blocking.js does not run; every other one it checks itself.
const UNSUPPORTED: &[&str] = &["watch-attr", "others", "shadow", "matches-prop", "matches-media", "nth-ancestor", "properties", "-abp-properties", "spath"];

// What is wrong with the part of a cosmetic rule after its first `#`: unbalanced
// brackets or quotes, or an operator Zitter does not run.
fn selector_problem(after_hash: &str) -> Option<String> {
    let (mut round, mut square, mut quote) = (0i32, 0i32, None::<char>);
    let mut chars = after_hash.char_indices().peekable();
    while let Some((at, c)) = chars.next() {
        if c == '\\' { chars.next(); continue; }
        if let Some(q) = quote { if c == q { quote = None; } continue; }
        match c {
            // Quotes count only in attribute values: `:has-text(Don't)` is fine.
            '"' | '\'' if square > 0 => quote = Some(c),
            '(' => round += 1,
            ')' => round -= 1,
            '[' => square += 1,
            ']' => square -= 1,
            ':' => {
                let name: String = after_hash[at + 1..].chars().take_while(|c| c.is_ascii_alphanumeric() || *c == '-').collect();
                if UNSUPPORTED.contains(&name.as_str()) && after_hash[at + 1 + name.len()..].starts_with('(') {
                    return Some(format!(":{name}() is not supported"));
                }
            }
            _ => {}
        }
        if round < 0 || square < 0 { return Some("unbalanced brackets".into()); }
    }
    if round != 0 || square != 0 || quote.is_some() { return Some("unbalanced brackets or quotes".into()); }
    None
}

fn within(host: &str, site: &str) -> bool {
    let host = host.trim().trim_end_matches('.').to_ascii_lowercase();
    let site = site.trim().trim_end_matches('.').to_ascii_lowercase();
    !site.is_empty() && (host == site || host.ends_with(&format!(".{site}")))
}

// The rule names only `site` (or its subdomains) as where it applies: a
// cosmetic rule's hostnames, a network rule's `domain=` option, or the host of
// a `||host` pattern. Negated entries (`~sub.example.com`) only narrow a rule.
fn rule_in_scope(rule: &str, site: &str) -> bool {
    let rule = rule.trim();
    if check_rule(rule).kind == 0 || is_comment(rule) { return false; }
    for separator in ["#@#", "##"] {
        if let Some(at) = rule.find(separator) {
            let domains: Vec<&str> = rule[..at].split(',').map(str::trim).filter(|d| !d.is_empty()).collect();
            let positive: Vec<&str> = domains.iter().copied().filter(|d| !d.starts_with('~')).collect();
            return !positive.is_empty() && positive.iter().all(|d| within(d, site))
                && domains.iter().filter_map(|d| d.strip_prefix('~')).all(|d| within(d, site));
        }
    }
    let body = rule.strip_prefix("@@").unwrap_or(rule);
    let (pattern, options) = match body.rfind('$') {
        Some(at) if !body[at + 1..].contains('/') => (&body[..at], &body[at + 1..]),
        _ => (body, ""),
    };
    for option in options.split(',') {
        let option = option.trim();
        if let Some(list) = option.strip_prefix("domain=").or_else(|| option.strip_prefix("from=")) {
            let entries: Vec<&str> = list.split('|').map(str::trim).filter(|d| !d.is_empty()).collect();
            let positive: Vec<&str> = entries.iter().copied().filter(|d| !d.starts_with('~')).collect();
            return !positive.is_empty() && positive.iter().all(|d| within(d, site));
        }
    }
    if let Some(rest) = pattern.strip_prefix("||") {
        let end = rest.find(|c: char| matches!(c, '^' | '/' | '*' | ':' | '|' | '?')).unwrap_or(rest.len());
        return end > 0 && within(&rest[..end], site);
    }
    false
}

fn count_rules(list: &str) -> usize {
    list.lines().filter(|line| !is_comment(line)).count()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn engine(rules: &str) -> Box<Engine> { new_engine(&vec![rules.to_string()]) }

    #[test]
    fn blocks_network_rules_by_type_and_party() {
        let e = engine("||ads.example^\n||tracker.test^$third-party\n/banner/*$image\n@@||ads.example/allowed.js");
        assert_eq!(e.check_request("https://ads.example/a.js", "https://news.test/", "script"), 1);
        assert_eq!(e.check_request("https://ads.example/allowed.js", "https://news.test/", "script"), 2);
        assert_eq!(e.check_request("https://tracker.test/p.gif", "https://news.test/", "image"), 1);
        assert_eq!(e.check_request("https://tracker.test/p.gif", "https://www.tracker.test/", "image"), 0);
        assert_eq!(e.check_request("https://cdn.test/banner/1.png", "https://news.test/", "image"), 1);
        assert_eq!(e.check_request("https://cdn.test/banner/1.js", "https://news.test/", "script"), 0);
        assert_eq!(e.check_request("https://news.test/app.js", "https://news.test/", "script"), 0);
        assert_eq!(e.rule_count(), 4);
    }

    #[test]
    fn important_wins_over_exceptions() {
        let e = engine("||ads.example^$important\n@@||ads.example^");
        assert_eq!(e.check_request("https://ads.example/x.js", "https://news.test/", "script"), 1);
    }

    #[test]
    fn several_lists_share_one_engine() {
        let e = new_engine(&vec!["||one.test^".into(), "||two.test^\n! comment".into()]);
        assert_eq!(e.check_request("https://two.test/x", "https://site.test/", "script"), 1);
        assert_eq!(e.check_request("https://one.test/x", "https://site.test/", "script"), 1);
        assert_eq!(e.rule_count(), 2);
    }

    #[test]
    fn returns_specific_and_generic_cosmetic_rules() {
        let e = engine("news.test##.sponsored\n##.ad-banner\n##div#top-ad\nnews.test#@#.ad-banner\nother.test##.promo");
        let page = e.cosmetic_resources("https://www.news.test/article");
        // Generic rules that are more than one class or ID come with every page.
        assert_eq!(page.hide_selectors, vec![".sponsored".to_string(), "div#top-ad".to_string()]);
        assert!(!page.generichide);
        let generic = e.hidden_class_id_selectors(&vec!["ad-banner".into(), "x".into()], &vec![], &page.exceptions);
        assert!(generic.is_empty());
        let elsewhere = e.cosmetic_resources("https://plain.test/");
        assert_eq!(elsewhere.hide_selectors, vec!["div#top-ad".to_string()]);
        let generic = e.hidden_class_id_selectors(&vec!["ad-banner".into()], &vec![], &elsewhere.exceptions);
        assert_eq!(generic, vec![".ad-banner".to_string()]);
    }

    #[test]
    fn passes_procedural_and_action_rules_to_the_page() {
        let e = engine("news.test##span:has-text(/^REKLAMA$/):upward(2)\nnews.test##.wall:remove()\nnews.test#?#div:-abp-has(.ad)");
        let page = e.cosmetic_resources("https://news.test/");
        // Without css-validation a procedural selector stays one string.
        assert!(page.hide_selectors.contains(&"span:has-text(/^REKLAMA$/):upward(2)".to_string()), "{:?}", page.hide_selectors);
        assert!(page.hide_selectors.contains(&"div:-abp-has(.ad)".to_string()), "{:?}", page.hide_selectors);
        assert_eq!(page.procedural.len(), 1);
        assert!(page.procedural[0].contains(r#""type":"remove""#) && page.procedural[0].contains(".wall"), "{}", page.procedural[0]);
    }

    #[test]
    fn generichide_turns_generic_rules_off() {
        let e = engine("##.ad-banner\n@@||news.test^$generichide");
        assert!(e.cosmetic_resources("https://news.test/").generichide);
        assert!(!e.cosmetic_resources("https://other.test/").generichide);
    }

    #[test]
    fn checks_single_rules() {
        assert_eq!(check_rule("||ads.example^$script").kind, 1);
        assert_eq!(check_rule("example.com##.ad").kind, 2);
        assert_eq!(check_rule("! a comment").kind, 3);
        assert_eq!(check_rule("example.com##+js(nowebrtc)").kind, 0);
        assert_eq!(check_rule("example.com#?#div:has-text(Ad)").kind, 2);
        assert_eq!(check_rule("example.com##span:has-text(/^REKLAMA$/):upward(2)").kind, 2);
        assert_eq!(check_rule("example.com##body:style(overflow: auto)").kind, 2);
        assert_eq!(check_rule("example.com##.wall:remove()").kind, 2);
        assert_eq!(check_rule("example.com##div:has-text(Ad").kind, 0);
        assert_eq!(check_rule("example.com##div:watch-attr(class)").kind, 0);
        assert_eq!(check_rule("||a.test^\n||b.test^").kind, 0);
        let bad = check_rule("||ads.example^$unknownoption");
        assert_eq!(bad.kind, 0);
        assert!(!bad.error.is_empty());
    }

    #[test]
    fn keeps_assistant_rules_on_their_site() {
        assert!(rule_in_scope("example.com##.ad", "example.com"));
        assert!(rule_in_scope("news.example.com,example.com##.ad", "example.com"));
        assert!(rule_in_scope("example.com#@#.ad", "example.com"));
        assert!(!rule_in_scope("##.ad", "example.com"));
        assert!(!rule_in_scope("example.com,other.com##.ad", "example.com"));
        assert!(!rule_in_scope("~example.com##.ad", "example.com"));
        assert!(rule_in_scope("||ads.net^$domain=example.com", "example.com"));
        assert!(rule_in_scope("||ads.net^$script,domain=www.example.com|example.com", "example.com"));
        assert!(!rule_in_scope("||ads.net^$domain=example.com|other.com", "example.com"));
        assert!(!rule_in_scope("||ads.net^", "example.com"));
        assert!(rule_in_scope("||example.com/ads/*", "example.com"));
        assert!(rule_in_scope("||static.example.com^$image", "example.com"));
        assert!(!rule_in_scope("||notexample.com^", "example.com"));
        assert!(!rule_in_scope("/ads/*", "example.com"));
        assert!(!rule_in_scope("! comment", "example.com"));
    }
}
