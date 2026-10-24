#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Wire slots from Microsoft's schemas (including the deprecated SBOX slot 2):
// https://github.com/microsoft/mxc/blob/6cd3d58f05d3447e67109cfb75e042803b843ca4/external/windows-sdk/BaseContainerSpecification.fbs
// https://github.com/microsoft/mxc/blob/main/external/windows-sdk/ProcessSecurityEnvironment.fbs
// A small forward FlatBuffer writer keeps these independent SDK-only PoCs free
// of a new library dependency. Unused fields are absent, not renumbered.
inline std::vector<std::uint8_t> BuildFilePolicy(
    bool psec, const std::string &bin, const std::string &sources,
    const std::string &secrets, bool allowSecrets, bool denyOverridesGrant)
{
    std::vector<std::uint8_t> b(8, 0);
    const auto u16 = [&](std::uint16_t v) {
        b.push_back(static_cast<std::uint8_t>(v));
        b.push_back(static_cast<std::uint8_t>(v >> 8));
    };
    const auto u32 = [&](std::uint32_t v) {
        for (unsigned shift = 0; shift < 32; shift += 8)
            b.push_back(static_cast<std::uint8_t>(v >> shift));
    };
    const auto patch = [&](size_t at, size_t value) {
        for (unsigned i = 0; i < 4; ++i)
            b[at + i] = static_cast<std::uint8_t>(value >> (8 * i));
    };
    const auto align = [&] { while (b.size() % 4) b.push_back(0); };
    const auto string = [&](const std::string &value) {
        align();
        const auto at = b.size();
        u32(static_cast<std::uint32_t>(value.size()));
        b.insert(b.end(), value.begin(), value.end());
        b.push_back(0);
        return at;
    };
    const unsigned slots = psec ? 9 : 12;
    const unsigned rwSlot = psec ? 4 : 7;
    const unsigned roSlot = psec ? 5 : 8;
    const unsigned denySlot = psec ? 6 : 11;
    const auto vtable = b.size();
    u16(static_cast<std::uint16_t>(4 + 2 * slots));
    u16(24); // table: soffset, version, AC/padding, RW, RO, deny
    for (unsigned slot = 0; slot < slots; ++slot) {
        u16(slot == 0 ? 4 : !psec && slot == 1 ? 8 :
            slot == rwSlot ? 12 : slot == roSlot ? 16 :
            slot == denySlot && !allowSecrets ? 20 : 0);
    }
    align();
    const auto table = b.size();
    b.resize(table + 24, 0);
    patch(0, table);
    const char *identifier = psec ? "PSEC" : "SBOX";
    for (unsigned i = 0; i < 4; ++i) b[4 + i] = identifier[i];
    patch(table, table - vtable);
    if (psec) {
        // SchemaVersion is an inline struct of two uint16, NOT a table offset.
        b[table + 4] = 1;
        b[table + 6] = 1;
    } else {
        b[table + 8] = 1; // app_container = true
        const auto version = string("0.1.0");
        patch(table + 4, version - (table + 4));
    }
    const auto paths = [&](size_t field, const std::vector<std::string> &values) {
        align();
        const auto at = b.size();
        patch(field, at - field);
        u32(static_cast<std::uint32_t>(values.size()));
        const auto elements = b.size();
        b.resize(elements + values.size() * 4, 0);
        for (size_t i = 0; i < values.size(); ++i) {
            const auto target = string(values[i]);
            const auto element = elements + i * 4;
            patch(element, target - element);
        }
    };
    std::vector<std::string> rw{sources};
    if (allowSecrets || denyOverridesGrant) rw.push_back(secrets);
    paths(table + 12, rw);
    paths(table + 16, {bin}); // native read-only path grant includes image execution
    if (!allowSecrets) paths(table + 20, {secrets});
    align();
    return b;
}
