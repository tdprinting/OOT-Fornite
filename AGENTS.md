# Working defaults

- Before substantial work, briefly state the approach and why. Keep updates and replies clear, concise, and free of repetition.
- Start with `mod/Royale/FEATURES.md`; read only the relevant feature and dependencies. Verify the branch and preserve existing edits.
- Use scoped `rg` searches and small source ranges. `.rgignore` excludes bulky search results; use explicit paths or `--no-ignore` when needed. Preserve useful comments.
- Use local Git first. Prefer RTK for supported verbose reads; query GitHub with selected `--json` fields, `--jq`, and bounded `--limit`. Inspect only relevant failed log sections; avoid unchanged polling.
- Validate affected behavior. Compile the mod for engine changes; device visuals and performance require device testing. Never claim runtime verification from build success.
- Keep one brief current handoff per feature: status, changed files, validation, next step. Link to archived history instead of repeating it.
