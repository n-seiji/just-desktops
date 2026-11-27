# just-desktops

A macOS desktop switcher in C and Objective-C, one process, no dependencies. `docs/architecture.md` maps the code.

- Run everything through `mise run <task>` (`build`, `test`, `test-san`, `app`). The scripts need `CFLAGS` from `mise.toml`.
- Before and after a change under `src/`, run `.claude/skills/verify-just-desktops/gate.sh`. A pass is `build=0 test=0 san=0 version=0 app=0 warnings=0`.
- Unit tests cover only the pure modules listed in `docs/architecture.md`. To change logic in an OS-coupled file, first move it into a pure module with a test. A new test needs a `run` line in `scripts/run-tests.sh`.
- A change you can see on screen is proved by driving the app: use the `verify-just-desktops` skill.
- Idle memory is the product. Read "Memory rules" in `docs/architecture.md` before adding a window, a timer or an AppKit call.
- `README.md` and `README.ja.md` are a pair. Change both together.
- A config key lives in five places: `src/config.c`, the `sample` string in `src/config_write.c`, `config.example`, the table in both READMEs, and the matching page in `docs/`.
- Each fact has one home in the docs. Link to it instead of repeating it.
- Releasing is in `docs/releasing.md`. Never run a release or change signing secrets without being asked.
