# Native image model mounting

Dreamscapes passes the selected Society model and the Society-owned generation
resource directory to iiLocalDiffusion. Model weights remain outside the app
bundle. A split denoiser is a supported model layout; it does not need to be
rewritten into a complete checkpoint.

For Krea2, the native SDK recognizes bare original/ComfyUI denoiser keys and the
`diffusion_model.`, `model.diffusion_model.` and `transformer.` namespaces.
The native context mounts the same tensor bytes under its canonical namespace.
Embedded text encoders and VAEs keep their component namespaces.

A split Krea2 model needs Qwen3-VL-4B and Wan2.1 VAE components. Missing components
are mounted from `Models/.generation-resources/iiLocalDiffusion/` in Society,
using the SDK generation resource catalog. The SDK validates the full external
tensor layouts before assembling the model. Explicit selections keep precedence;
embedded components are retained. Partial embedded text encoders are rejected by
context validation rather than replaced silently. Required components remain
enabled when optional style modifiers are disabled.

`scripts/provision_generation_resources.py` installs checksum-verified resources
from an installed SDK. Repeating the command preserves existing resource files.
Adding catalog entries stages and verifies all new files before atomically
publishing the updated manifest. A catalog update that would change a previously
installed payload identity is rejected. Check mode never updates the store.

```sh
python3 scripts/provision_generation_resources.py \
  --source "$HOME/.local/SDK/iiLocalDiffusion/share/iiLocalDiffusion/resources" \
  --society /absolute/path/to/Society.societycontainer
```

Model enumeration must also use the current Society storage SDK. A synchronized
catalog can temporarily precede the local owner's full-file indexing; the
current SDK reconciles local file metadata so available checkpoint files remain
visible. Rebuilding the application refreshes its embedded SDK runtime.

`Dreamscapes.GenerationResourceProvisioning` covers additive upgrades, interrupted
publication, preserved file identity and incompatible-resource rejection.
`Dreamscapes.GenerationResources` rejects weights in the app bundle. The native
SDK's `NativeKrea2NamesTests`, `NativeLlmConfigTests` and VAE contract tests cover
mounting behavior. `NativeComponentMountIntegration` is an opt-in real-checkpoint
resident preparation and 128px/one-step inference check; it does not certify
normal-resolution generation speed or image quality.

The native module also mounts the original Qwen3-VL `model.language_model` text tower without converting or rewriting encoder files. Resource audits include text encoders and stream their SHA-256 checks to avoid loading multi-gigabyte payloads into memory.

Generation and local-Society fixtures use temporary `.societycontainer` packages, matching the current storage SDK source boundary. This does not relax rejection of bare directories, redirected roots or nested source containers.
