import { WASI } from 'node:wasi';
import { spawn } from 'node:child_process';
import { readFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';

const runner = fileURLToPath(import.meta.url);

async function runGuest(binary) {
  const wasi = new WASI({ version: 'preview1', args: [binary], env: {} });
  try {
    const module = await WebAssembly.compile(await readFile(binary));
    const instance = await WebAssembly.instantiate(module, wasi.getImportObject());
    process.exitCode = wasi.start(instance);
  } catch (error) {
    console.error(`[trap] ${error.message}`);
    process.exitCode = 134;
  }
}

async function runChecked(mode, binary, ...expected) {
  if (!['success', 'crash'].includes(mode) || !binary || expected.length === 0) {
    console.error('usage: run-wasi-test.mjs <success|crash> <binary> <expected text>...');
    return 2;
  }

  const child = spawn(process.execPath, [runner, '--guest', binary], {
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
    child.on('close', (code, signal) => resolve({ code, signal }));
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
  await runGuest(process.argv[3]);
} else {
  process.exitCode = await runChecked(...process.argv.slice(2));
}
