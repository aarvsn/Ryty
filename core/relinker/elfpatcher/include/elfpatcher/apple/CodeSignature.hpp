#ifndef ELFPATCHER_APPLE_CODESIGNATURE_HPP
#define ELFPATCHER_APPLE_CODESIGNATURE_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Elfpatcher::Apple {

// Ad-hoc code signature support for the macOS and iOS Mach-O writers.
//
// The embedded SuperBlob mirrors what `codesign -s -` produces for the subset
// of slots the kernel checks on launch: a CodeDirectory with per-page SHA-256
// hashes over everything in front of the signature, plus (2.0.0) an optional
// CSSLOT_ENTITLEMENTS entry whose digest occupies special slot -5.

struct AdHocSignatureSpec {
    const std::uint8_t* image = nullptr;   // bytes [0, codeLimit) get hashed
    std::size_t codeLimit = 0;             // file offset of the signature itself
    std::string identifier;                // CodeDirectory identifier string
    const std::vector<std::uint8_t>* entitlements = nullptr; // may be null
    bool hardenedRuntime = false;
};

// Exact size in bytes the SuperBlob will occupy for the given inputs.
// `entitlements` may be null (no entitlements slice, 2.0.0 layout).
std::uint32_t AdHocSignatureSize(std::size_t codeLimit, std::size_t identifierLength,
                                 const std::vector<std::uint8_t>* entitlements);

// Builds the embedded signature blob. `spec.image[0, codeLimit)` is hashed
// page by page (4096-byte pages, SHA-256).
std::vector<std::uint8_t> BuildAdHocSignature(const AdHocSignatureSpec& spec);

struct SignatureVerifyResult {
    bool ok = false;
    std::string reason;
    std::string identifier;
    bool hasEntitlements = false;
};

// Locates LC_CODE_SIGNATURE inside a thin Mach-O slice. Returns false when the
// command is absent or the load commands are malformed.
bool FindCodeSignature(const std::uint8_t* image, std::size_t imageSize,
                       std::uint32_t& signatureOffset, std::uint32_t& signatureSize);

// Full verification: parses the SuperBlob, re-computes every code page hash
// and every special-slot hash that has a matching blob, and checks the
// CodeDirectory fields. `imageSize` is the complete slice size (the signature
// must close the file).
SignatureVerifyResult VerifyAdHocSignature(const std::uint8_t* image, std::size_t imageSize);

}

#endif
