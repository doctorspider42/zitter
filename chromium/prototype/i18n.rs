// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
// UI language. Polish is the source language: every UI string is written in
// Polish where it is used and wrapped in t()/tf() (Rust) or T()/TF() (C++).
// With English active the Polish text is looked up in the English catalogue
// (i18n_en_*.rs); a missing entry falls back to Polish. tests/test_i18n.py checks
// that every wrapped string has an entry.
use std::collections::HashMap;
#[cfg(not(test))]
use std::sync::atomic::{AtomicU8, Ordering};
use std::sync::OnceLock;

mod en_scene { include!("i18n_en_scene.rs"); }
mod en_shell { include!("i18n_en_shell.rs"); }
mod en_native { include!("i18n_en_native.rs"); }

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Language { Polish, English }

// Process-wide in the app; per thread in unit tests, so a test switching to
// English never leaks into tests running beside it.
#[cfg(not(test))]
static LANGUAGE: AtomicU8 = AtomicU8::new(0);
#[cfg(not(test))]
fn load() -> u8 { LANGUAGE.load(Ordering::Relaxed) }
#[cfg(not(test))]
fn store(value: u8) { LANGUAGE.store(value, Ordering::Relaxed) }
#[cfg(test)]
thread_local! { static LANGUAGE: std::cell::Cell<u8> = const { std::cell::Cell::new(0) }; }
#[cfg(test)]
fn load() -> u8 { LANGUAGE.with(|l| l.get()) }
#[cfg(test)]
fn store(value: u8) { LANGUAGE.with(|l| l.set(value)) }

/// A locale such as "pl", "pl-PL", "en-US"; anything that is not Polish is English.
pub fn set_language(locale: &str) {
    let polish = locale.is_empty() || locale.to_ascii_lowercase().starts_with("pl");
    store(if polish { 0 } else { 1 });
}

pub fn language() -> Language {
    if load() == 0 { Language::Polish } else { Language::English }
}

fn english() -> &'static HashMap<&'static str, &'static str> {
    static TABLE: OnceLock<HashMap<&'static str, &'static str>> = OnceLock::new();
    TABLE.get_or_init(|| {
        en_scene::EN.iter().chain(en_shell::EN).chain(en_native::EN).copied().collect()
    })
}

/// The UI text for a Polish source string.
pub fn t(polish: &'static str) -> &'static str {
    match language() {
        Language::Polish => polish,
        Language::English => english().get(polish).copied().unwrap_or(polish),
    }
}

/// The same for text that is not a literal (it crosses the C++ bridge).
pub fn translate(polish: &str) -> String {
    match language() {
        Language::Polish => polish.to_owned(),
        Language::English => english().get(polish).map_or_else(|| polish.to_owned(), |s| (*s).to_owned()),
    }
}

/// A translated template whose `{}` placeholders take `args` in order.
pub fn tf(polish: &'static str, args: &[&str]) -> String {
    fill(t(polish), args)
}

pub fn fill(template: &str, args: &[&str]) -> String {
    let mut out = String::with_capacity(template.len() + args.iter().map(|a| a.len()).sum::<usize>());
    let mut rest = template;
    let mut args = args.iter();
    while let Some(at) = rest.find("{}") {
        out.push_str(&rest[..at]);
        out.push_str(args.next().copied().unwrap_or(""));
        rest = &rest[at + 2..];
    }
    out.push_str(rest);
    out
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn english_catalogue_translates_templates_and_falls_back_to_polish() {
        assert_eq!(t("Anuluj"), "Anuluj");
        set_language("en-US");
        assert_eq!(language(), Language::English);
        assert_eq!(t("Anuluj"), "Cancel");
        assert_eq!(t("Tekst bez tłumaczenia"), "Tekst bez tłumaczenia");
        assert_eq!(translate("Anuluj"), "Cancel");
        set_language("pl-PL");
        assert_eq!(t("Anuluj"), "Anuluj");
        assert_eq!(fill("Usunięto {} z {}", &["a", "b"]), "Usunięto a z b");
        assert_eq!(fill("{} i {}", &["a"]), "a i ");
        for table in [en_scene::EN, en_shell::EN, en_native::EN] {
            for (polish, english) in table {
                assert!(!english.is_empty(), "{polish}");
                assert_eq!(polish.matches("{}").count(), english.matches("{}").count(), "{polish}");
            }
        }
    }
}
