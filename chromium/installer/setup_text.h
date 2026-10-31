// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
// The installer's text (design: Instalator). Polish is the source, as in the
// app; English on systems whose UI language is not Polish.
#ifndef ZITTER_CHROMIUM_INSTALLER_SETUP_TEXT_H_
#define ZITTER_CHROMIUM_INSTALLER_SETUP_TEXT_H_

#include <windows.h>

namespace zitter::setup {

struct Text {
  const wchar_t* eyebrow_install;
  const wchar_t* eyebrow_found;
  const wchar_t* eyebrow_remove;
  const wchar_t* where;
  const wchar_t* for_me;
  const wchar_t* for_me_detail;
  const wchar_t* for_all;
  const wchar_t* for_all_detail;
  const wchar_t* folder;
  const wchar_t* change;
  const wchar_t* desktop_shortcut;
  const wchar_t* platform;
  const wchar_t* cancel;
  const wchar_t* install;
  const wchar_t* already_here;
  const wchar_t* installed;
  const wchar_t* update_keeps;  // %s: the new version
  const wchar_t* remove_zitter;
  const wchar_t* install_elsewhere;
  const wchar_t* update;
  const wchar_t* remove_question;
  const wchar_t* remove_from;  // %s: the folder
  const wchar_t* remove_data;
  const wchar_t* remove_data_detail;
  const wchar_t* back;
  const wchar_t* about_to_tremble;
  const wchar_t* copying_for_me;
  const wchar_t* copying_for_all;
  const wchar_t* removing;
  const wchar_t* copying_files;
  const wchar_t* removing_files;
  const wchar_t* close;
  const wchar_t* close_zitter_first;
  const wchar_t* failed;  // %s: the reason
  const wchar_t* pick_folder;
};

inline const Text& Strings() {
  static const Text kPolish = {
      L"Instalacja",
      L"Instalacja · znaleziono Zitter",
      L"Odinstalowanie",
      L"Gdzie ma zamieszkać?",
      L"Tylko dla mnie",
      L"Bez uprawnień administratora",
      L"Dla wszystkich na tym komputerze",
      L"Windows zapyta o uprawnienia",
      L"Folder",
      L"Zmień…",
      L"Skrót na pulpicie",
      L"x64 · Windows 10+",
      L"Anuluj",
      L"Zainstaluj",
      L"Zitter już tu jest.",
      L"✓ Zainstalowany",
      L"Aktualizacja zachowa przestrzenie, karty, profile i hasła. Nowa wersja: %s.",
      L"Usuń Zitter",
      L"Zainstaluj w innym miejscu",
      L"Zaktualizuj",
      L"Usunąć Zitter?",
      L"Usunę aplikację z %s razem ze skrótami.",
      L"Usuń też dane przeglądania",
      L"Przestrzenie, profile, historia, hasła i karty. Tego nie da się cofnąć.",
      L"Wróć",
      L"Zaraz zadrży.",
      L"Kopiuję Zitter na ten komputer. Bez uprawnień administratora, tylko dla Ciebie.",
      L"Kopiuję Zitter dla wszystkich użytkowników tego komputera.",
      L"Usuwam Zitter z tego komputera.",
      L"Kopiuję pliki przeglądarki",
      L"Usuwam pliki przeglądarki",
      L"Zamknij",
      L"Zamknij Zitter i spróbuj ponownie.",
      L"Nie udało się: %s",
      L"Wybierz folder dla Zitter",
  };
  static const Text kEnglish = {
      L"Installation",
      L"Installation · Zitter found",
      L"Uninstall",
      L"Where should it live?",
      L"Just for me",
      L"No administrator rights",
      L"For everyone on this computer",
      L"Windows will ask for permission",
      L"Folder",
      L"Change…",
      L"Desktop shortcut",
      L"x64 · Windows 10+",
      L"Cancel",
      L"Install",
      L"Zitter is already here.",
      L"✓ Installed",
      L"Updating keeps your spaces, tabs, profiles and passwords. New version: %s.",
      L"Remove Zitter",
      L"Install somewhere else",
      L"Update",
      L"Remove Zitter?",
      L"I'll remove the app from %s, shortcuts included.",
      L"Also remove browsing data",
      L"Spaces, profiles, history, passwords and tabs. This can't be undone.",
      L"Back",
      L"About to tremble.",
      L"Copying Zitter to this computer. No administrator rights, just for you.",
      L"Copying Zitter for everyone on this computer.",
      L"Removing Zitter from this computer.",
      L"Copying browser files",
      L"Removing browser files",
      L"Close",
      L"Close Zitter and try again.",
      L"That didn't work: %s",
      L"Choose a folder for Zitter",
  };
  return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_POLISH ? kPolish : kEnglish;
}

}  // namespace zitter::setup

#endif
