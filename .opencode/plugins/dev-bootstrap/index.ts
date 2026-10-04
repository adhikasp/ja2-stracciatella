// Auto-bootstrap the JA2 worktree when an OpenCode session starts in it.
//
// Spawns `python tools/dev.py bootstrap --auto` (see AGENTS.md): it configures
// the build directory, runs `uv sync`, checks game_dir and builds in the
// background. dev.py dedupes repeated calls and never blocks, so session start
// stays fast and loading this plugin twice is harmless.
//
// The same bootstrap runs from `.claude/settings.json` (SessionStart) for
// Claude Code sessions.
import { spawn } from "node:child_process"
import { existsSync } from "node:fs"
import { join } from "node:path"

const INTERPRETERS = process.platform === "win32" ? ["python", "py", "python3"] : ["python3", "python"]

export default {
  id: "ja2.dev-bootstrap",
  async setup(ctx: { location?: { directory?: string } }) {
    const root = ctx?.location?.directory ?? process.cwd()
    const script = join(root, "tools", "dev.py")
    if (!existsSync(script)) return
    for (const interpreter of INTERPRETERS) {
      const child = spawn(interpreter, [script, "bootstrap", "--auto"], {
        cwd: root,
        detached: true,
        stdio: "ignore",
      })
      const spawned = await new Promise<boolean>((resolve) => {
        child.once("error", () => resolve(false))
        child.once("spawn", () => resolve(true))
      })
      if (spawned) {
        child.unref()
        return
      }
    }
    console.error(`[ja2] dev-bootstrap: no python interpreter found to run ${script}`)
  },
}
