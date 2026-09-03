import { spawn, type ChildProcessWithoutNullStreams } from "node:child_process";
import { EventEmitter } from "node:events";
import { createInterface, type Interface } from "node:readline";

export interface Vec3 extends Array<number> {
  0: number;
  1: number;
  2: number;
  length: 3;
}
export type Vec = [number, number, number];

export interface EngineOptions {
  /** Path to engine.exe (or the binary on your platform). */
  binary: string;
  /** Extra CLI args. `--headless` is added unless `window` is true. */
  args?: string[];
  /** Show a window instead of running headless. */
  window?: boolean;
  scene?: string;
  width?: number;
  height?: number;
  /** Working directory for the engine process (defaults to the binary's folder). */
  cwd?: string;
}

export interface Response {
  id: number | null;
  ok: boolean;
  result?: any;
  error?: string;
}

export interface EngineEvent {
  event: string;
  [k: string]: unknown;
}

/**
 * A connection to one ali-engine process. Every call writes one JSON line to
 * stdin and resolves with the matching JSON line from stdout. Engine-emitted
 * events (no `id`) are surfaced via the `"event"` EventEmitter channel.
 *
 * ```ts
 * const eng = await AliEngine.launch({ binary: "./engine.exe" });
 * await eng.send("scene.reset");
 * await eng.spawn("ball", { primitive: "sphere", position: [0, 5, 0], body: { type: "dynamic" } });
 * await eng.step(1 / 60, 120);
 * await eng.screenshot("out.png");
 * await eng.close();
 * ```
 */
export class AliEngine extends EventEmitter {
  private proc: ChildProcessWithoutNullStreams;
  private rl: Interface;
  private nextId = 1;
  private pending = new Map<number, { resolve: (r: any) => void; reject: (e: Error) => void }>();
  private closed = false;

  private constructor(opts: EngineOptions) {
    super();
    const args = [...(opts.args ?? [])];
    if (!opts.window) args.push("--headless");
    if (opts.scene) args.push("--scene", opts.scene);
    if (opts.width) args.push("--width", String(opts.width));
    if (opts.height) args.push("--height", String(opts.height));

    this.proc = spawn(opts.binary, args, {
      cwd: opts.cwd,
      stdio: ["pipe", "pipe", "pipe"],
    }) as ChildProcessWithoutNullStreams;

    this.proc.stderr.setEncoding("utf8");
    this.proc.stderr.on("data", (d: string) => this.emit("log", d));
    this.proc.on("exit", (code) => {
      this.closed = true;
      for (const { reject } of this.pending.values())
        reject(new Error(`engine exited (${code}) with the request still pending`));
      this.pending.clear();
      this.emit("exit", code);
    });

    this.rl = createInterface({ input: this.proc.stdout });
    this.rl.on("line", (line) => this.onLine(line));
  }

  /** Spawn the engine and wait for its `ready` event. */
  static launch(opts: EngineOptions): Promise<AliEngine> {
    const eng = new AliEngine(opts);
    return new Promise((resolve, reject) => {
      const to = setTimeout(() => reject(new Error("engine did not report ready within 10s")), 10_000);
      eng.once("ready", () => {
        clearTimeout(to);
        resolve(eng);
      });
      eng.once("exit", (code) => {
        clearTimeout(to);
        reject(new Error(`engine exited before ready (${code})`));
      });
    });
  }

  private onLine(line: string) {
    line = line.trim();
    if (!line) return;
    let msg: Response | EngineEvent;
    try {
      msg = JSON.parse(line);
    } catch {
      this.emit("log", line + "\n");
      return;
    }
    if ("event" in msg) {
      this.emit("event", msg);
      this.emit(msg.event, msg);
      return;
    }
    const r = msg as Response;
    const id = typeof r.id === "number" ? r.id : undefined;
    const waiter = id !== undefined ? this.pending.get(id) : undefined;
    if (!waiter) return; // response to a fire-and-forget or an unknown id
    this.pending.delete(id!);
    if (r.ok) waiter.resolve(r.result ?? {});
    else waiter.reject(new Error(r.error ?? "command failed"));
  }

  /** Send one command and resolve with its `result` (rejects on `ok:false`). */
  send<T = any>(method: string, params: Record<string, unknown> = {}): Promise<T> {
    if (this.closed) return Promise.reject(new Error("engine connection is closed"));
    const id = this.nextId++;
    const payload = JSON.stringify({ id, method, params }) + "\n";
    return new Promise<T>((resolve, reject) => {
      this.pending.set(id, { resolve, reject });
      this.proc.stdin.write(payload, (err) => {
        if (err) {
          this.pending.delete(id);
          reject(err);
        }
      });
    });
  }

  // ---- typed convenience wrappers over the common commands ----
  ping() {
    return this.send<{ pong: true }>("ping");
  }
  loadScene(path: string) {
    return this.send<{ entities: number }>("scene.load", { path });
  }
  saveScene(path?: string) {
    return this.send<{ path: string }>("scene.save", path ? { path } : {});
  }
  resetScene() {
    return this.send("scene.reset");
  }
  state() {
    return this.send<any>("scene.state");
  }
  spawn(name: string, opts: Record<string, unknown> = {}) {
    return this.send<{ name: string }>("entity.spawn", { name, ...opts });
  }
  destroy(name: string) {
    return this.send("entity.destroy", { name });
  }
  setTransform(name: string, t: { position?: Vec; rotation?: Vec; scale?: Vec }) {
    return this.send("entity.setTransform", { name, ...t });
  }
  setMaterial(name: string, m: Record<string, unknown>) {
    return this.send("entity.setMaterial", { name, ...m });
  }
  camera(c: { position?: Vec; target?: Vec; fov_deg?: number }) {
    return this.send("camera.set", c);
  }
  /** Advance the simulation `steps` times by `dt` seconds each. */
  step(dt = 1 / 60, steps = 1) {
    return this.send<{ stepped: number }>("world.step", { dt, steps });
  }
  screenshot(path?: string, size?: { width: number; height: number }) {
    return this.send<{ path: string; width: number; height: number }>("observe.screenshot", {
      ...(path ? { path } : {}),
      ...(size ?? {}),
    });
  }
  observe() {
    return this.send<any>("observe.entities");
  }
  stats() {
    return this.send<any>("observe.stats");
  }
  render(env: Record<string, unknown>) {
    return this.send<{ environment: any }>("render.set", env);
  }

  /** Send `quit` and wait for the process to exit. */
  async close(): Promise<void> {
    if (this.closed) return;
    try {
      await this.send("quit");
    } catch {
      /* the engine may exit before answering */
    }
    await new Promise<void>((resolve) => {
      if (this.closed) return resolve();
      this.proc.once("exit", () => resolve());
      setTimeout(() => {
        this.proc.kill();
        resolve();
      }, 2000);
    });
  }
}

export default AliEngine;
