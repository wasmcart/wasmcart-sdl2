// input_test.mjs - drive input_cart through the reference Node host and check
// the SDL events the wasmcart video backend makes from the pointer array and
// the scroll wheel.
//
//   ./build.sh && node input_test.mjs
//
// Uses wasmcart from `npm install` in this directory, or a sibling checkout
// with its node_modules installed, or WASMCART=/path/to/wasmcart. Exits 1 on
// any failure.
//
// Every frame pumps SDL at least four times (see input_cart.c), so a count
// above 1 for a single notch means the backend re-read per-frame host state on
// every pump.
import { fileURLToPath, pathToFileURL } from 'node:url';
import { dirname, join, resolve } from 'node:path';
import { existsSync } from 'node:fs';

const HERE = dirname(fileURLToPath(import.meta.url));
// WASMCART env, else an npm install here (what CI uses), else a sibling checkout.
const LOCAL = join(HERE, 'node_modules', 'wasmcart');
const WASMCART = resolve(process.env.WASMCART
  || (existsSync(LOCAL) ? LOCAL : join(HERE, '..', '..', '..', 'wasmcart')));
const { CartHost } = await import(pathToFileURL(join(WASMCART, 'index.js')).href);

let fails = 0;
const ck = (what, got, want) => {
  const ok = typeof want === 'function' ? want(got) : Object.is(got, want);
  console.log(`${ok ? '  ok  ' : '*** FAIL'} ${what}: ${got}${ok || typeof want === 'function' ? '' : ` (want ${want})`}`);
  if (!ok) fails++;
};

const cart = new CartHost();
await cart.load(join(HERE, 'build'));
const v = (n) => cart.readDebugValue(n).value;
const frame = () => cart.runFrame();

ck('cart declared a wheel buffer', cart.getInfo().wheelPtr > 0, true);
frame();

// ── slot 0: mouse ─────────────────────────────────────────────────────────
cart.setPointer(0, 100, 50, 0, 1);
frame();
ck('mouse: one motion', v('motions'), 1);
ck('mouse: x', v('mouse_x'), 100);
ck('mouse: y', v('mouse_y'), 50);
frame();
ck('mouse: an unmoved pointer sends no motion', v('motions'), 1);

cart.setPointer(0, 101, 50, 1, 1); frame();
ck('mouse: left down once', v('down_l'), 1);
ck('mouse: moved with the press', v('motions'), 2);
cart.setPointer(0, 101, 50, 0, 1); frame();
ck('mouse: left up once', v('up_l'), 1);
cart.setPointer(0, 101, 50, 2, 1); frame();
cart.setPointer(0, 101, 50, 0, 1); frame();
ck('mouse: right down', v('down_r'), 1);
ck('mouse: right up', v('up_r'), 1);
cart.setPointer(0, 101, 50, 4, 1); frame();
cart.setPointer(0, 101, 50, 0, 1); frame();
ck('mouse: middle down', v('down_m'), 1);
ck('mouse: middle up', v('up_m'), 1);

cart.setPointer(0, 101, 50, 1, 1); frame();
cart.setPointer(0, 101, 50, 1, 0); frame();
ck('mouse: going inactive releases a held button', v('up_l'), 2);

// ── wheel ─────────────────────────────────────────────────────────────────
cart.wheel(0, 120); frame();
ck('wheel: one notch is one event, not one per pump', v('wheel_events'), 1);
ck('wheel: integer y', v('wheel_y_int'), 1);
ck('wheel: precise y', v('wheel_py'), 1);
frame();
ck('wheel: no input, no event', v('wheel_events'), 1);

cart.wheel(0, 60); frame();
cart.wheel(0, 60); frame();
ck('wheel: two half notches are fractional', v('wheel_py'), 2);
ck('wheel: and accumulate to one integer notch', v('wheel_y_int'), 2);

cart.wheel(0, -240); frame();
ck('wheel: down is negative', v('wheel_py'), 0);
cart.wheel(240, 0); frame();
ck('wheel: horizontal, right positive', v('wheel_px'), 2);

// ── slots 1-9: touch ──────────────────────────────────────────────────────
const tmBefore = v('touch_mouse_events');
cart.setPointer(1, 160, 120, 1, 1); frame();
ck('touch: finger down once', v('finger_down'), 1);
ck('touch: x in cart pixels', v('finger_x'), 160);
ck('touch: y in cart pixels', v('finger_y'), 120);
frame();
ck('touch: an unmoved finger sends no motion', v('finger_motion'), 0);
cart.setPointer(1, 200, 100, 1, 1); frame();
ck('touch: finger motion once', v('finger_motion'), 1);
ck('touch: moved x', v('finger_x'), 200);
cart.setPointer(2, 10, 10, 1, 1); frame();
ck('touch: second finger', v('finger_down'), 2);
cart.setPointer(1, 200, 100, 0, 0);
cart.setPointer(2, 10, 10, 0, 0); frame();
ck('touch: both lift', v('finger_up'), 2);
ck('touch: SDL synthesized mouse events for touch', v('touch_mouse_events'), (n) => n > tmBefore);

cart.destroy();
console.log(fails ? `\n${fails} FAILED` : '\nall passed');
process.exit(fails ? 1 : 0);
