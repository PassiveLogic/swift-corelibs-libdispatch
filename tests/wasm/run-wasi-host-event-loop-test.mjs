import { WASI } from 'node:wasi';
import { spawn } from 'node:child_process';
import { readFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';

const runner = fileURLToPath(import.meta.url);

async function runGuest(binary, inlineSchedule) {
  const wasi = new WASI({ version: 'preview1', args: [binary], env: {} });
  let instance;
  let scheduledTurns = 0;
  let hostTimerCallbacks = 0;
  let timer = null;
  let complete;
  let targetResult;

  const waitForResult = target => {
    targetResult = target;
    return new Promise(resolve => { complete = resolve; });
  };

  const waitWithTimeout = async promise => {
    let watchdog;
    const timedOut = new Promise((_, reject) => {
      watchdog = setTimeout(
          () => reject(new Error(
              `host-driven Dispatch timed out waiting for ${targetResult}`)),
          5000);
    });
    await Promise.race([promise, timedOut]);
    clearTimeout(watchdog);
  };

  const updateTimer = () => {
    const delayNs = instance.exports.host_event_loop_next_timer_delay();
    if (timer !== null) {
      clearTimeout(timer);
      timer = null;
    }
    if (delayNs === -1n) return;
    const delayMs = Number((delayNs + 999999n) / 1000000n);
    timer = setTimeout(() => {
      timer = null;
      hostTimerCallbacks++;
      runTurn(false);
    }, delayMs);
  };

  const runTurn = scheduledTurn => {
    const scheduledBefore = scheduledTurns;
    const immediateBefore = instance.exports.host_event_loop_immediate_count();
    const moreWork = (scheduledTurn ?
      instance.exports.host_event_loop_perform() :
      instance.exports.host_event_loop_perform_timer()) !== 0;
    const immediateAfter = instance.exports.host_event_loop_immediate_count();
    if (immediateAfter - immediateBefore > 1) {
      throw new Error('one perform step drained multiple root items');
    }
    const requestedTurns = scheduledTurns - scheduledBefore;
    if (requestedTurns !== Number(moreWork)) {
      throw new Error(
          `perform returned ${moreWork} but requested ${requestedTurns} turns`);
    }
    if (instance.exports.host_event_loop_result() === targetResult) {
      const resolve = complete;
      complete = null;
      resolve();
    } else {
      updateTimer();
    }
  };

  const imports = wasi.getImportObject();
  imports.dispatch_host = {
    schedule() {
      scheduledTurns++;
      if (inlineSchedule) {
        runTurn(true);
      } else {
        queueMicrotask(() => runTurn(true));
      }
    },
  };

  const module = await WebAssembly.compile(await readFile(binary));
  instance = await WebAssembly.instantiate(module, imports);
  wasi.initialize(instance);
  instance.exports.host_event_loop_initialize();

  const completed = waitForResult(200);
  const resultAtReturn = instance.exports.host_event_loop_submit();
  if (resultAtReturn !== 0) {
    throw new Error(`Dispatch ran before submission returned: ${resultAtReturn}`);
  }
  if (scheduledTurns !== 1) {
    throw new Error(`initial submissions scheduled ${scheduledTurns} host turns`);
  }

  await waitWithTimeout(completed);
  if (instance.exports.host_event_loop_result() !== 200) {
    throw new Error(`unexpected first result: ${instance.exports.host_event_loop_result()}`);
  }

  const turnsBeforeSecondBurst = scheduledTurns;
  const secondAtReturn = instance.exports.host_event_loop_submit_second_burst();
  if (secondAtReturn !== 0 || scheduledTurns !== turnsBeforeSecondBurst + 1) {
    throw new Error('second burst did not schedule one later host turn');
  }
  await new Promise(resolve => setImmediate(resolve));
  if (instance.exports.host_event_loop_result() !== 201) {
    throw new Error(`unexpected final result: ${instance.exports.host_event_loop_result()}`);
  }

  const turnsBeforeTimerOnly = scheduledTurns;
  const timerCallbacksBeforeTimerOnly = hostTimerCallbacks;
  const timerOnlyCompleted = waitForResult(211);
  const timerAtReturn = instance.exports.host_event_loop_submit_timer_only();
  if (timerAtReturn !== 0 || scheduledTurns !== turnsBeforeTimerOnly + 1) {
    throw new Error('timer-only submission did not schedule one later host turn');
  }
  await waitWithTimeout(timerOnlyCompleted);
  if (hostTimerCallbacks !== timerCallbacksBeforeTimerOnly + 1) {
    throw new Error('timer-only submission was not driven by one host timeout');
  }
  if (instance.exports.host_event_loop_next_timer_delay() !== -1n) {
    throw new Error('timer remained armed after firing');
  }

  console.log(`host event loop OK turns=${scheduledTurns}`);
}

async function checkRejected(binary, guestMode, expected, successMessage) {
  const child = spawn(process.execPath, [runner, guestMode, binary], {
    stdio: ['ignore', 'pipe', 'pipe'],
  });
  let output = '';
  child.stdout.setEncoding('utf8');
  child.stderr.setEncoding('utf8');
  child.stdout.on('data', data => { output += data; });
  child.stderr.on('data', data => { output += data; });
  const result = await new Promise((resolve, reject) => {
    const timeout = setTimeout(() => {
      child.kill('SIGKILL');
      reject(new Error('inline scheduler child timed out'));
    }, 2000);
    child.once('error', error => {
      clearTimeout(timeout);
      reject(error);
    });
    child.once('close', (code, signal) => {
      clearTimeout(timeout);
      resolve({ code, signal });
    });
  });
  if (result.code === 0 && result.signal === null) {
    throw new Error(`${guestMode} unexpectedly succeeded`);
  }
  if (!output.includes(expected)) {
    throw new Error(`missing ${guestMode} diagnostic:\n${output}`);
  }
  console.log(successMessage);
}

// Work submitted before the host registers its scheduler stays pending (no
// turn can be requested yet) and is handed over at registration: exactly one
// turn, and the work runs on it.
async function runLateRegistrationGuest(binary) {
  const wasi = new WASI({ version: 'preview1', args: [binary], env: {} });
  let scheduledTurns = 0;
  const imports = wasi.getImportObject();
  imports.dispatch_host = { schedule() { scheduledTurns++; } };
  const module = await WebAssembly.compile(await readFile(binary));
  const instance = await WebAssembly.instantiate(module, imports);
  wasi.initialize(instance);
  const resultAtReturn = instance.exports.host_event_loop_submit();
  if (resultAtReturn !== 0 || scheduledTurns !== 0) {
    throw new Error(`unregistered submission ran=${resultAtReturn} ` +
        `turns=${scheduledTurns}`);
  }
  instance.exports.host_event_loop_initialize();
  if (scheduledTurns !== 1) {
    throw new Error(`registration scheduled ${scheduledTurns} turns`);
  }
  let moreWork = instance.exports.host_event_loop_perform() !== 0;
  while (moreWork) {
    if (scheduledTurns !== 2) {
      throw new Error(`retained turn miscounted: ${scheduledTurns}`);
    }
    moreWork = instance.exports.host_event_loop_perform() !== 0;
  }
  if (instance.exports.host_event_loop_result() !== 200) {
    throw new Error(`late registration result ${instance.exports.host_event_loop_result()}`);
  }
  console.log('late registration handed pending work to the host');
}

// Registration from inside a drained work item: the drain step that runs the
// registering block hands the still-pending signal block to the host (one
// turn), the top-level wait then runs it, and the host's turn finds nothing.
async function runRegisterInDrainGuest(binary) {
  const wasi = new WASI({ version: 'preview1', args: [binary], env: {} });
  let scheduledTurns = 0;
  const imports = wasi.getImportObject();
  imports.dispatch_host = { schedule() { scheduledTurns++; } };
  const module = await WebAssembly.compile(await readFile(binary));
  const instance = await WebAssembly.instantiate(module, imports);
  wasi.initialize(instance);
  instance.exports.host_event_loop_register_from_work_item();
  const atRegistration =
      instance.exports.host_event_loop_schedule_calls_at_registration();
  if (atRegistration !== 0) {
    throw new Error(`registration inside the drain requested ${atRegistration} turns itself`);
  }
  if (scheduledTurns !== 1) {
    throw new Error(`registration in a drain scheduled ${scheduledTurns} turns`);
  }
  const moreWork = instance.exports.host_event_loop_perform() !== 0;
  if (moreWork || scheduledTurns !== 1) {
    throw new Error(`turn after in-drain registration: more=${moreWork} ` +
        `turns=${scheduledTurns}`);
  }
  console.log('registration inside a drain handed pending work to the host');
}

async function runTimerRaceGuest(binary) {
  const wasi = new WASI({ version: 'preview1', args: [binary], env: {} });
  let scheduledTurns = 0;
  const imports = wasi.getImportObject();
  imports.dispatch_host = { schedule() { scheduledTurns++; } };
  const module = await WebAssembly.compile(await readFile(binary));
  const instance = await WebAssembly.instantiate(module, imports);
  wasi.initialize(instance);
  instance.exports.host_event_loop_initialize();
  instance.exports.host_event_loop_submit();
  if (scheduledTurns !== 1) {
    throw new Error(`submission scheduled ${scheduledTurns} turns`);
  }
  const immediateBefore = instance.exports.host_event_loop_immediate_count();
  const moreWork = instance.exports.host_event_loop_perform_timer() !== 0;
  const immediateAfter = instance.exports.host_event_loop_immediate_count();
  if (immediateAfter !== immediateBefore + 1) {
    throw new Error('timer perform did not drain exactly one root item');
  }
  if (!moreWork) {
    throw new Error('timer perform did not report retained work');
  }
  if (scheduledTurns !== 1) {
    throw new Error(`timer perform duplicated scheduled turn: ${scheduledTurns}`);
  }
  console.log('timer perform preserved scheduled turn');
}

const [modeOrBinary, maybeBinary] = process.argv.slice(2);
if (!modeOrBinary) {
  console.error('usage: run-wasi-host-event-loop-test.mjs [mode] <binary>');
  process.exitCode = 2;
} else if (modeOrBinary === '--inline-check') {
  await checkRejected(maybeBinary, '--inline-guest',
      'WASI event loop scheduler invoked perform inline',
      'inline host callback rejected');
} else if (modeOrBinary === '--inline-guest') {
  await runGuest(maybeBinary, true);
} else if (modeOrBinary === '--late-registration') {
  await runLateRegistrationGuest(maybeBinary);
} else if (modeOrBinary === '--register-in-drain') {
  await runRegisterInDrainGuest(maybeBinary);
} else if (modeOrBinary === '--timer-race') {
  await runTimerRaceGuest(maybeBinary);
} else {
  await runGuest(modeOrBinary, false);
}
