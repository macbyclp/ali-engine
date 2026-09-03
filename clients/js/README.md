# ali-engine-client

Drive [ali-engine](https://github.com/macbyclp/ali-engine) from Node.js over its
line-delimited JSON protocol. Spawns the engine binary, matches every request to
its response, and surfaces engine events.

```bash
npm install ali-engine-client
```

```ts
import { AliEngine } from "ali-engine-client";

const eng = await AliEngine.launch({ binary: "./engine.exe", width: 1280, height: 720 });

eng.on("log", (s) => process.stderr.write(s));

await eng.resetScene();
await eng.spawn("ground", { primitive: "plane", scale: [20, 1, 20], body: { type: "static" } });
await eng.spawn("ball", {
  primitive: "sphere",
  position: [0, 6, 0],
  body: { type: "dynamic", restitution: 0.7 },
});
await eng.camera({ position: [6, 4, 8], target: [0, 1, 0] });
await eng.step(1 / 60, 180);
await eng.screenshot("bounce.png");

await eng.close();
```

## API

- `AliEngine.launch(opts)` → resolves once the engine reports `ready`.
  `opts`: `{ binary, args?, window?, scene?, width?, height?, cwd? }`.
- `send(method, params?)` → `Promise<result>`; rejects on `{ ok: false }`.
- Typed wrappers: `ping`, `loadScene`, `saveScene`, `resetScene`, `state`,
  `spawn`, `destroy`, `setTransform`, `setMaterial`, `camera`, `step`,
  `screenshot`, `observe`, `stats`, `render`.
- Events: `"ready"`, `"event"` (every engine event), the event name itself
  (e.g. `"scene.reloaded"`), `"log"` (stderr), `"exit"`.
- `close()` sends `quit` and waits for the process to exit.

Anything not wrapped is one `send()` call away — the full method list is in
[`docs/AI-PROTOCOL.md`](../../docs/AI-PROTOCOL.md).

MIT
