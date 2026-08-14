import { WASI } from 'node:wasi';
import { spawn } from 'node:child_process';
import { readFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';

const runner = fileURLToPath(import.meta.url);

async function runGuest(binary, denyFdPoll) {
  const wasi = new WASI({ version: 'preview1', args: [binary], env: {} });
  const importObject = wasi.getImportObject();
  let memory = null;
  if (denyFdPoll) {
    // Emulate a limited host (browser WASI shims): reject poll_oneoff calls
    // carrying fd_read/fd_write subscriptions with ENOTSUP, pass clock-only
    // sets through. Preview1 subscriptions are 48 bytes with the union tag
    // at byte 8 (0 = clock).
    const preview1 = importObject.wasi_snapshot_preview1;
    const realPoll = preview1.poll_oneoff;
    preview1.poll_oneoff = (inPtr, outPtr, nsubscriptions, neventsPtr) => {
      const view = new DataView(memory.buffer);
      for (let i = 0; i < nsubscriptions; i++) {
        if (view.getUint8(inPtr + i * 48 + 8) !== 0) {
          return 58; // __WASI_ERRNO_NOTSUP
        }
      }
      return realPoll(inPtr, outPtr, nsubscriptions, neventsPtr);
    };
  }
  try {
    const module = await WebAssembly.compile(await readFile(binary));
    const instance = await WebAssembly.instantiate(module, importObject);
    memory = instance.exports.memory;
    process.exitCode = wasi.start(instance);
  } catch (error) {
    console.error(`[trap] ${error.message}`);
    process.exitCode = 134;
  }
}

async function runChecked(...argv) {
  // --stdin-after <ms>:<text> pipes <text> to the guest's stdin after a
  // delay, so readiness-driven tests prove the guest genuinely parks in the
  // host poll instead of finding data already buffered.
  let stdinAfter = null;
  let denyFdPoll = false;
  for (;;) {
    if (argv[0] === '--stdin-after') {
      const spec = argv[1] ?? '';
      const colon = spec.indexOf(':');
      stdinAfter = { ms: Number(spec.slice(0, colon)), text: spec.slice(colon + 1) };
      argv = argv.slice(2);
      if (colon < 1 || !Number.isFinite(stdinAfter.ms)) {
        console.error('bad --stdin-after spec, want <ms>:<text>');
        return 2;
      }
    } else if (argv[0] === '--deny-fd-poll') {
      denyFdPoll = true;
      argv = argv.slice(1);
    } else {
      break;
    }
  }
  const [mode, binary, ...expected] = argv;
  if (!['success', 'crash'].includes(mode) || !binary || expected.length === 0) {
    console.error('usage: run-wasi-test.mjs [--stdin-after <ms>:<text>] [--deny-fd-poll] <success|crash> <binary> <expected text>...');
    return 2;
  }

  const guestArgs = ['--guest'];
  if (denyFdPoll) guestArgs.push('--deny-fd-poll');
  guestArgs.push(binary);
  const child = spawn(process.execPath, [runner, ...guestArgs], {
    stdio: [stdinAfter ? 'pipe' : 'ignore', 'pipe', 'pipe'],
  });
  let stdinTimer = null;
  if (stdinAfter) {
    // EPIPE from a guest that already exited is not a runner failure
    child.stdin.on('error', () => {});
    stdinTimer = setTimeout(() => {
      if (child.exitCode === null && child.signalCode === null &&
          child.stdin.writable) {
        child.stdin.write(stdinAfter.text);
        child.stdin.end();
      }
    }, stdinAfter.ms);
  }
  let stdout = '';
  let stderr = '';
  child.stdout.setEncoding('utf8');
  child.stderr.setEncoding('utf8');
  child.stdout.on('data', data => { stdout += data; });
  child.stderr.on('data', data => { stderr += data; });

  const result = await new Promise((resolve, reject) => {
    child.on('error', reject);
    child.on('close', (code, signal) => {
      if (stdinTimer) clearTimeout(stdinTimer);
      resolve({ code, signal });
    });
  });
  process.stdout.write(stdout);
  process.stderr.write(stderr);

  const crashed = result.signal !== null || result.code !== 0;
  const output = stdout + stderr;
  if ((mode === 'crash') !== crashed) {
    console.error(`expected ${mode}, observed exit=${result.code} signal=${result.signal}`);
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
  const denyFdPoll = process.argv[3] === '--deny-fd-poll';
  await runGuest(process.argv[denyFdPoll ? 4 : 3], denyFdPoll);
} else {
  process.exitCode = await runChecked(...process.argv.slice(2));
}
