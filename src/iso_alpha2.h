// SPDX-License-Identifier: CC-BY-4.0
// Copyright (c) 2025-2026 Walkthru.Earth (https://walkthru.earth)
#pragma once

#include <cstddef>
#include <string_view>

// ISO 3166-1 assigned alpha-2 codes, snapshot 2026-10-09.
// UN M49 ISO-alpha2 table plus ISO's TW assignment. Country or area codes do
// not assert sovereignty and are never inferred from an H3 cell.
// https://unstats.un.org/unsd/methodology/m49/overview/
// https://www.iso.org/iso-3166-country-codes.html
// https://www.iso.org/obp/ui/#iso:code:3166:TW
namespace embedded_h3::location::detail {

inline constexpr std::size_t kIsoAlpha2Count = 249;

// Two letters per code, no separators, sorted. Lives in flash .rodata.
inline constexpr std::string_view kIsoAlpha2 =
    "ADAEAFAGAIALAMAOAQARASATAUAWAXAZBABBBDBEBFBGBHBIBJBLBMBNBOBQBRBSBTBVBWBYBZ"
    "CACCCDCFCGCHCICKCLCMCNCOCRCUCVCWCXCYCZDEDJDKDMDODZECEEEGEHERESETFIFJFKFMFO"
    "FRGAGBGDGEGFGGGHGIGLGMGNGPGQGRGSGTGUGWGYHKHMHNHRHTHUIDIEILIMINIOIQIRISITJE"
    "JMJOJPKEKGKHKIKMKNKPKRKWKYKZLALBLCLILKLRLSLTLULVLYMAMCMDMEMFMGMHMKMLMMMNMO"
    "MPMQMRMSMTMUMVMWMXMYMZNANCNENFNGNINLNONPNRNUNZOMPAPEPFPGPHPKPLPMPNPRPSPTPW"
    "PYQARERORSRURWSASBSCSDSESGSHSISJSKSLSMSNSOSRSSSTSVSXSYSZTCTDTFTGTHTJTKTLTM"
    "TNTOTRTTTVTWTZUAUGUMUSUYUZVAVCVEVGVIVNVUWFWSYEYTZAZMZW";

// Build time proof that the table holds exactly the documented count of
// distinct uppercase codes in strictly ascending order.
consteval bool iso_alpha2_table_ok() {
  if (kIsoAlpha2.size() != kIsoAlpha2Count * 2)
    return false;
  for (std::size_t i = 0; i < kIsoAlpha2.size(); ++i)
    if (kIsoAlpha2[i] < 'A' || kIsoAlpha2[i] > 'Z')
      return false;
  for (std::size_t i = 2; i < kIsoAlpha2.size(); i += 2)
    if (kIsoAlpha2.substr(i - 2, 2) >= kIsoAlpha2.substr(i, 2))
      return false;
  return true;
}
static_assert(iso_alpha2_table_ok(), "ISO alpha-2 table is malformed");

} // namespace embedded_h3::location::detail
