# Third-party notices

This product includes third-party software and model files.

## Zenz v3.2 small GGUF

- Repository: Miwa-Keita/zenz-v3.2-small-gguf
- Source: Hugging Face
- Original model file: ggml-model-Q5_K_M.gguf
- Included file: models/zenz-v3.2-small-Q5_K_M.gguf
- License: Apache License 2.0
- Source repository URL: https://huggingface.co/Miwa-Keita/zenz-v3.2-small-gguf
- Source file URL: https://huggingface.co/Miwa-Keita/zenz-v3.2-small-gguf/blob/main/ggml-model-Q5_K_M.gguf
- Source commit observed for the model upload: c67e03e
- Notes: Distributed without modification except for file placement, file naming, and packaging into the MSI installer.

## llama.cpp / ggml runtime

- Project: ggml-org/llama.cpp
- Source repository URL: https://github.com/ggml-org/llama.cpp
- Source tag: b10437
- Source commit: 16d222fc5ead59d20039501a37251c9ed457a454
- Mozkey tokenizer compatibility patch SHA256: 6DB5C11B2DA8415D6B37200EC6AA4F3FDCDDE8EFB1E7A71375FE331A7B0E829A
- Patched src/llama-vocab.cpp SHA256: 7E07061170256C9F5A5ACF6AE30D5C6E5A0CBB89D706EEDAE95AC5F9B46230F1
- Source assets:
  - x64/llama-server.exe
  - arm64/llama-server.exe
- Installed file name: llama-server.exe
- Runtime policy: CPU-only; ggml/llama are statically linked into llama-server.exe.
- Microsoft Visual C++ runtime DLLs remain external dependencies and are supplied by the architecture-specific MSI runtime payload.
- License: MIT License

## Privacy note

Zenz live correction is designed to run locally. The bundled runtime is started as a local process and the HTTP inference endpoint is expected to bind to 127.0.0.1 only. User input is not intentionally sent to external servers by this feature.
Zenz feedback learning can be disabled in the settings UI. Left context is sanitized before being included in prompts; sensitive-looking context such as URLs, email addresses, file paths, tokens, and long numeric identifiers is rejected before prompt construction.
