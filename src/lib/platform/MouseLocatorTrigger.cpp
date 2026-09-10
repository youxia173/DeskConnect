/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/MouseLocatorTrigger.h"

#include "common/MouseLocatorBinding.h"
#include "common/Settings.h"
#include "deskflow/KeyMap.h"

#include <cstdlib>
#include <string>

namespace {

constexpr KeyModifierMask kIgnoreMask =
    KeyModifierAltGr | KeyModifierCapsLock | KeyModifierNumLock | KeyModifierScrollLock;

enum class BindingType
{
  None,
  Button,
  Key
};

struct Binding
{
  BindingType type = BindingType::None;
  ButtonID button = kButtonNone;
  KeyID key = kKeyNone;
  KeyModifierMask mask = 0;
};

bool parseBinding(const std::string &raw, Binding &out)
{
  out = Binding{};
  if (raw.empty()) {
    out.type = BindingType::Button;
    out.button = kButtonMiddle;
    return true;
  }

  const std::string mousePrefix = "mousebutton(";
  const std::string keyPrefix = "keystroke(";
  if (raw.size() >= 2 && raw.back() == ')') {
    if (raw.compare(0, mousePrefix.size(), mousePrefix) == 0) {
      std::string inner = raw.substr(mousePrefix.size(), raw.size() - mousePrefix.size() - 1);
      KeyModifierMask mask = 0;
      if (!deskflow::KeyMap::parseModifiers(inner, mask)) {
        return false;
      }
      char *end = nullptr;
      const auto button = static_cast<ButtonID>(strtol(inner.c_str(), &end, 10));
      if (end == nullptr || *end != '\0' || button <= 0) {
        return false;
      }
      out.type = BindingType::Button;
      out.button = button;
      out.mask = mask;
      return true;
    }
    if (raw.compare(0, keyPrefix.size(), keyPrefix) == 0) {
      std::string inner = raw.substr(keyPrefix.size(), raw.size() - keyPrefix.size() - 1);
      KeyModifierMask mask = 0;
      if (!deskflow::KeyMap::parseModifiers(inner, mask)) {
        return false;
      }
      KeyID key = kKeyNone;
      if (!deskflow::KeyMap::parseKey(inner, key) || key == kKeyNone) {
        return false;
      }
      out.type = BindingType::Key;
      out.key = key;
      out.mask = mask;
      return true;
    }
  }
  return false;
}

Binding loadBinding()
{
  Binding binding;
  const auto value = Settings::value(Settings::Core::MouseLocatorBinding).toString().toStdString();
  if (!parseBinding(value, binding)) {
    parseBinding(MouseLocatorBinding::kDefault, binding);
  }
  return binding;
}

bool modifiersMatch(KeyModifierMask actual, KeyModifierMask required)
{
  return (actual & ~kIgnoreMask) == (required & ~kIgnoreMask);
}

} // namespace

bool MouseLocatorTrigger::matchesButton(ButtonID button, KeyModifierMask mask)
{
  const Binding binding = loadBinding();
  if (binding.type != BindingType::Button) {
    return false;
  }
  return button == binding.button && modifiersMatch(mask, binding.mask);
}

bool MouseLocatorTrigger::matchesKey(KeyID key, KeyModifierMask mask)
{
  const Binding binding = loadBinding();
  if (binding.type != BindingType::Key || key == kKeyNone) {
    return false;
  }
  return key == binding.key && modifiersMatch(mask, binding.mask);
}
