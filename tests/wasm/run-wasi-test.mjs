import { WASI } from 'node:wasi';
import { spawn } from 'node:child_process';
import { readFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';

const runner = fileURLToPath(import.meta.url);

async function runGuest(binary, guestOpts) {
  const wasi = new WASI({
    version: 'preview1',
    args: [binary],
    env: {},
    preopens: guestOpts.preopens,
  });
  const importObject = wasi.getImportObject();
  let memory = null;
  if (guestOpts.wallClockOffset !== null) {
    // Emulate a host wall-clock adjustment: after a delay, shift every
    // CLOCK_REALTIME reading (clock id 0) by a fixed offset. The other
    // clocks are untouched.
    const preview1 = importObject.wasi_snapshot_preview1;
    const realClockTimeGet = preview1.clock_time_get;
    const start = performance.now();
    const { afterMs, offsetMs } = guestOpts.wallClockOffset;
    preview1.clock_time_get = (id, precision, timePtr) => {
      const rc = realClockTimeGet(id, precision, timePtr);
      if (rc === 0 && id === 0 && performance.now() - start >= afterMs) {
        const view = new DataView(memory.buffer);
        const time = view.getBigUint64(timePtr, true);
        view.setBigUint64(timePtr,
            time + BigInt(offsetMs) * 1000000n, true);
      }
      return rc;
    };
  }
  try {
    const module = await WebAssembly.compile(await readFile(binary));
    const instance = await WebAssembly.instantiate(module, importObject);
    memory = instance.exports.memory;
    process.exitCode = wasi.start(instance);
  } catch (error) {
    console.error(`[trap] ${error.message}`);
    // 134 = 128 + SIGABRT, the status of a native abort()
    process.exitCode = 134;
  }
}

async function runChecked(...argv) {
  const guestFlags = [];
  for (;;) {
    if (argv[0] === '--preopen') {
      const spec = argv[1] ?? '';
      if (spec.indexOf(':') < 1) {
        console.error('bad --preopen spec, want <guest dir>:<host dir>');
        return 2;
      }
      guestFlags.push(argv[0], argv[1]);
      argv = argv.slice(2);
    } else if (argv[0] === '--wall-clock-offset') {
      const spec = argv[1] ?? '';
      const colon = spec.indexOf(':');
      const afterMs = Number(spec.slice(0, colon));
      const offsetMs = Number(spec.slice(colon + 1));
      if (colon < 1 || !Number.isInteger(afterMs) ||
          !Number.isInteger(offsetMs)) {
        console.error('bad --wall-clock-offset spec, want <ms>:<offset ms>');
        return 2;
      }
      guestFlags.push(argv[0], argv[1]);
      argv = argv.slice(2);
    } else {
      break;
    }
  }
  const [mode, binary, ...expected] = argv;
  if (!['success', 'crash'].includes(mode) || !binary ||
      expected.length === 0) {
    console.error('usage: run-wasi-test.mjs [--preopen <guest>:<host>] ' +
        '[--wall-clock-offset <ms>:<offset ms>] <success|crash> <binary> ' +
        '<expected text>...');
    return 2;
  }

  const guestArgs = ['--guest', ...guestFlags, binary];
  const child = spawn(process.execPath, [runner, ...guestArgs], {
    stdio: ['ignore', 'pipe', 'pipe'],
  });
  let stdout = '';
  let stderr = '';
  child.stdout.setEncoding('utf8');
  child.stderr.setEncoding('utf8');
  child.stdout.on('data', data => { stdout += data; });
  child.stderr.on('data', data => { stderr += data; });

  const result = await new Promise((resolve, reject) => {
    child.on('error', reject);
    child.on('close', (code, signal) => {
      resolve({ code, signal });
    });
  });
  process.stdout.write(stdout);
  process.stderr.write(stderr);

  const crashed = result.signal !== null || result.code !== 0;
  const output = stdout + stderr;
  if ((mode === 'crash') !== crashed) {
    console.error(`expected ${mode}, observed exit=${result.code} ` +
        `signal=${result.signal}`);
    return 1;
  }
  for (const text of expected) {
    if (!output.includes(text)) {
      console.error(`expected output was not found: ${text}`);
      return 1;
    }
  }
  return 0;
}

if (process.argv[2] === '--guest') {
  const guestOpts = {
    wallClockOffset: null,
    preopens: {},
  };
  let i = 3;
  for (;; i++) {
    if (process.argv[i] === '--preopen') {
      const spec = process.argv[i + 1];
      const colon = spec.indexOf(':');
      guestOpts.preopens[spec.slice(0, colon)] = spec.slice(colon + 1);
      i++;
    } else if (process.argv[i] === '--wall-clock-offset') {
      const spec = process.argv[i + 1];
      const colon = spec.indexOf(':');
      guestOpts.wallClockOffset = {
        afterMs: Number(spec.slice(0, colon)),
        offsetMs: Number(spec.slice(colon + 1)),
      };
      i++;
    } else {
      break;
    }
  }
  await runGuest(process.argv[i], guestOpts);
} else {
  process.exitCode = await runChecked(...process.argv.slice(2));
}
