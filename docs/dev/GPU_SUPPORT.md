# GPU support

Ryty's AGC driver (libSceAgcDriver) renders through Vulkan. It is
vendor-agnostic — every feature it requires is queried before use — but
1.0.7 adds explicit vendor awareness where it changes outcomes.

## Vendor support matrix

| Vendor | Support | Notes |
| ------ | ------- | ----- |
| AMD | Reference | The guest GPU the PS5 ports from is an AMD RDNA device; the recompiler's ISA decoder targets RDNA/GFX10 and the host driver was developed on AMD. |
| NVIDIA | Supported (1.0.7) | Vendor-aware device selection, subgroup-size pinning via `VK_EXT_subgroup_size_control`, per-vendor shader variants. See below. |
| Intel | Best effort | Arc (Xe) meets the hard requirements (Vulkan 1.1, buffer device address, 8-bit storage, float controls, BC compression). Older integrated HD graphics usually lack pieces of that set and are rejected with a named missing feature. |
| Apple (MoltenVK) | Not supported | The macOS port renders through Vulkan as well; MoltenVK lacks parts of the required feature set. On macOS the Metal bridge exists for window/presentation plumbing only (see [building](../user/BUILDING.md)). |

## Hard requirements (all vendors)

A device is eligible when it offers, at minimum:

* Vulkan 1.1 with a graphics+compute queue (and a swapchain for windowed
  presentation),
* `VK_KHR_buffer_device_address` with `bufferDeviceAddress` + `shaderInt64`,
* `VK_KHR_8bit_storage` with `storageBuffer8BitAccess`,
* `VK_KHR_shader_float_controls` with `shaderSignedZeroInfNanPreserveFloat32`,
* `vertexPipelineStoresAndAtomics`, `fragmentStoresAndAtomics`,
  `samplerAnisotropy`, `textureCompressionBC`.

Everything else (descriptor indexing, timeline semaphores, mesh shaders,
barycentric interpolants, depth clip control, indirect draw counts, host
memory import, subgroup size control) is optional and gated per device.

## What 1.0.7 changes for NVIDIA

* **Device selection is scored, not first-match.** Discrete GPUs outrank
  integrated ones, so a laptop with an NVIDIA dGPU + Intel iGPU no longer
  depends on the ICD's enumeration order. Selection can be controlled from the
  environment (see below).
* **Subgroup sizes are pinned.** The recompiler lays guest wave64 programs out
  for the host's reported subgroup width (and splits workgroups when a 32-lane
  host runs a wave64 guest). With `VK_EXT_subgroup_size_control` available
  (NVIDIA advertises it, default 32 lanes) the driver now pins every compute
  pipeline to exactly that width, so the driver's scheduler can never run a
  variant at a width its SPIR-V was not generated for. Debug opt-out:
  `APS5_NO_SUBGROUP_PIN=1`.
* **Shader variants are per-vendor.** `SpirvTarget` carries the PCI vendor id;
  the recompile cache key, the serialized request format (v6) and the shader
  disk cache separate variants per vendor, and each SPIR-V module records its
  target vendor as an `OpModuleProcessed` annotation
  (`ryty-target-vendor:010de`) for tooling.
* **Guest wave32/64 emulation is exercised on 32-lane hosts** (the mesh/wave
  splitting path in `Graphics/Pipeline.cpp` and `ShaderValidation.cpp`).

## Selection controls (environment of the ported game)

| Variable | Effect |
| -------- | ------ |
| `RTY_GPU_VENDOR=<amd\|nvidia\|intel>` | Restrict the eligible devices to that PCI vendor before scoring. |
| `RTY_GPU_INDEX=<n>` | Pin the zero-based index into the eligible candidate list (checked after vendor filtering). Out-of-range values fail the port with a clear message. |

The driver logs every candidate with vendor name/type/score and its pick
(`[gpu] candidate ... (selected)`), so a wrong pick is diagnosable from the
log. Pipeline caches are already keyed by vendor/device/driver/UUID, so
switching devices never reuses another GPU's pipelines.

## Testing

The relinker and recompiler test suites cover the vendor plumbing
(`CacheKey`, request serialization v6). GPU behaviour itself needs real
hardware; the compatibility matrix in
[COMPATIBILITY.md](../user/COMPATIBILITY.md) records tested configurations.
