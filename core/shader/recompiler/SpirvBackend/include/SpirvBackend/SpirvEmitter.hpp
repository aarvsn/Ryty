#ifndef CORE_SHADER_RECOMPILIER_SPIRVBACKEND_INCLUDE_SPIRVBACKEND_SPIRVEMITTER_HPP
#define CORE_SHADER_RECOMPILIER_SPIRVBACKEND_INCLUDE_SPIRVBACKEND_SPIRVEMITTER_HPP

#include "IntermediateRepresentation/IrProgram.hpp"
#include "Optimization/BindingAllocator.hpp"
#include "Optimization/ShaderStageInputInfo.hpp"
#include <cstdint>
#include <vector>
#include <span>
#include <string_view>

namespace ShaderRecompiler {

struct SpirvTargetOptions {
    std::uint32_t vulkanVersion;
    std::uint32_t spirvVersion;
    std::uint32_t subgroupSize;
    std::uint32_t bdaAbiVersion;
    std::span<const std::uint32_t> supportedCapabilities;
    std::span<const std::string_view> supportedExtensions;
    // PCI vendor id of the target GPU and the pinned subgroup width it offers
    // (see SpirvTarget); the module preamble records both so SPIR-V tooling can
    // tell vendor-specific variants apart.
    std::uint32_t vendorId = 0;
    bool subgroupSizeControl = false;
    std::uint32_t maxSubgroupSize = 0;
};

class SpirvEmitter {
public:
    [[nodiscard]] std::vector<std::uint32_t> Emit(const IrProgram& program, const BindingAllocationResult& bindings, const SpirvTargetOptions& target) const;
    [[nodiscard]] std::vector<std::uint32_t> Emit(const IrProgram& program, const ShaderStageInputInfo& inputInfo, const BindingAllocationResult& bindings, const SpirvTargetOptions& target) const;

};

}

#endif
