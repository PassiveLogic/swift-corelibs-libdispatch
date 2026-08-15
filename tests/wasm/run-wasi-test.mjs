import { WASI } from 'node:wasi';
import { spawn } from 'node:child_process';
import { readFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';

const runner = fileURLToPath(import.meta.url);

async function runGuest(binary, guestOpts) {
  const wasi = new WASI({ version: 'preview1', args: [binary], env: {} });
  const importObject = wasi.getImportObject();
  let memory = null;
  if (guestOpts.denyFdPoll) {
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
  if (guestOpts.fdPollEbadfAfter !== null) {
    // Emulate wasmtime's close-while-armed shape: when an fd in the poll
    // set is no longer open, the whole poll_oneoff call fails with BADF
    // (rather than reporting a per-subscription error). Let the first n
    // fd-carrying polls through (the registration capability probe), then
    // fail every later one.
    const preview1 = importObject.wasi_snapshot_preview1;
    const realPoll = preview1.poll_oneoff;
    let fdPolls = 0;
    preview1.poll_oneoff = (inPtr, outPtr, nsubscriptions, neventsPtr) => {
      const view = new DataView(memory.buffer);
      let hasFd = false;
      for (let i = 0; i < nsubscriptions; i++) {
        if (view.getUint8(inPtr + i * 48 + 8) !== 0) hasFd = true;
      }
      if (hasFd && ++fdPolls > guestOpts.fdPollEbadfAfter) {
        return 8; // __WASI_ERRNO_BADF
      }
      return realPoll(inPtr, outPtr, nsubscriptions, neventsPtr);
    };
  }
  if (guestOpts.suppressPollHangup) {
    // Emulate a host that never reports FD_READWRITE_HANGUP (wasmtime 47
    // for pipes, empirically): EOF then shows up only as permanent
    // readable-with-nothing readiness. Preview1 events are 32 bytes:
    // userdata u64 @0, error u16 @8, type u8 @10, fd_readwrite
    // { nbytes u64 @16, flags u16 @24 }; hangup is flags bit 0x1.
    const preview1 = importObject.wasi_snapshot_preview1;
    const realPoll = preview1.poll_oneoff;
    preview1.poll_oneoff = (inPtr, outPtr, nsubscriptions, neventsPtr) => {
      const rc = realPoll(inPtr, outPtr, nsubscriptions, neventsPtr);
      if (rc === 0) {
        const view = new DataView(memory.buffer);
        const nevents = view.getUint32(neventsPtr, true);
        for (let i = 0; i < nevents; i++) {
          const type = view.getUint8(outPtr + i * 32 + 10);
          if (type === 1 || type === 2) { // fd_read / fd_write
            const flagsPtr = outPtr + i * 32 + 24;
            view.setUint16(flagsPtr, view.getUint16(flagsPtr, true) & ~1,
                true);
          }
        }
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
    process.exitCode = 134;
  }
}

async function runChecked(...argv) {
  // --stdin-after <ms>:<text> pipes <text> to the guest's stdin after a
  // delay, so readiness-driven tests prove the guest genuinely parks in the
  // host poll instead of finding data already buffered.
  let stdinAfter = null;
  const guestFlags = [];
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
    } else if (argv[0] === '--deny-fd-poll' ||
        argv[0] === '--suppress-poll-hangup') {
      guestFlags.push(argv[0]);
      argv = argv.slice(1);
    } else if (argv[0] === '--fd-poll-ebadf-after') {
      const n = Number(argv[1]);
      if (!Number.isInteger(n) || n < 0) {
        console.error('bad --fd-poll-ebadf-after count, want an integer');
        return 2;
      }
      guestFlags.push(argv[0], argv[1]);
      argv = argv.slice(2);
    } else {
      break;
    }
  }
  const [mode, binary, ...expected] = argv;
  if (!['success', 'crash'].includes(mode) || !binary || expected.length === 0) {
    console.error('usage: run-wasi-test.mjs [--stdin-after <ms>:<text>] [--deny-fd-poll] [--suppress-poll-hangup] [--fd-poll-ebadf-after <n>] <success|crash> <binary> <expected text>...');
    return 2;
  }

  const guestArgs = ['--guest', ...guestFlags, binary];
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
  const guestOpts = {
    denyFdPoll: false,
    suppressPollHangup: false,
    fdPollEbadfAfter: null,
  };
  let i = 3;
  for (;; i++) {
    if (process.argv[i] === '--deny-fd-poll') {
      guestOpts.denyFdPoll = true;
    } else if (process.argv[i] === '--suppress-poll-hangup') {
      guestOpts.suppressPollHangup = true;
    } else if (process.argv[i] === '--fd-poll-ebadf-after') {
      guestOpts.fdPollEbadfAfter = Number(process.argv[i + 1]);
      i++;
    } else {
      break;
    }
  }
  await runGuest(process.argv[i], guestOpts);
} else {
  process.exitCode = await runChecked(...process.argv.slice(2));
}
