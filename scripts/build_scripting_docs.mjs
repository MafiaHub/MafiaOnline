import { cp, mkdir, readFile, rm, writeFile } from "node:fs/promises";
import { spawnSync } from "node:child_process";
import path from "node:path";
import { fileURLToPath } from "node:url";

const projectRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const apiRoot = path.join(projectRoot, "scripting-api");
const configPath = path.join(apiRoot, "reference.config.json");
const buildRoot = path.join(projectRoot, "build");
const checkOnly = process.argv.includes("--check");

function option(name, fallback) {
  const index = process.argv.indexOf(name);
  if (index < 0) return fallback;
  if (!process.argv[index + 1]) throw new Error(`${name} needs a value`);
  return process.argv[index + 1];
}

function buildPath(value) {
  const resolved = path.resolve(projectRoot, value);
  if (!resolved.startsWith(`${buildRoot}${path.sep}`)) {
    throw new Error(`Generated output must be inside ${buildRoot}`);
  }
  return resolved;
}

function runCli(version, args) {
  const localEntrypoint = process.env.MAFIAHUB_SERVICES_CLI_ENTRYPOINT;
  const executable = localEntrypoint ? process.execPath : "npm";
  const argv = localEntrypoint
    ? [path.resolve(localEntrypoint), ...args]
    : ["exec", "--yes", `--package=@mafiahub/services-cli@${version}`, "--", "mafiahub-services", ...args];
  const onWindows = !localEntrypoint && process.platform === "win32";
  const commandArgs = onWindows ? argv.map((argument) => (/[\s"]/.test(argument) ? JSON.stringify(argument) : argument)) : argv;
  const result = spawnSync(executable, commandArgs, {
    cwd: projectRoot,
    env: {
      ...process.env,
      npm_config_cache: path.join(buildRoot, "npm-cache"),
      npm_config_update_notifier: "false",
    },
    stdio: "inherit",
    shell: onWindows,
  });
  if (result.error) throw result.error;
  if (result.status !== 0) throw new Error(`services-cli exited with status ${result.status}`);
}

async function buildContract(config, outputRoot) {
  await rm(outputRoot, { recursive: true, force: true });
  await mkdir(path.join(outputRoot, "targets"), { recursive: true });
  await cp(path.join(apiRoot, "shared.d.ts"), path.join(outputRoot, "targets", "shared.d.ts"));

  const targets = {};
  for (const [name, target] of Object.entries(config.targets)) {
    const targetRoot = path.join(outputRoot, "targets", name);
    await mkdir(targetRoot, { recursive: true });
    await cp(path.join(apiRoot, target.generatedEntryPoint), path.join(targetRoot, "api.d.ts"));
    await cp(path.join(apiRoot, target.metadataFile), path.join(targetRoot, "metadata.json"));
    const tsconfig = JSON.parse(await readFile(path.join(apiRoot, target.tsconfig), "utf8"));
    tsconfig.files = ["../shared.d.ts", "api.d.ts"];
    await writeFile(path.join(targetRoot, "tsconfig.json"), `${JSON.stringify(tsconfig, null, 2)}\n`);
    targets[name] = {
      label: target.label,
      entryPoint: `targets/${name}/api.d.ts`,
      tsconfig: `targets/${name}/tsconfig.json`,
      description: target.description,
    };
  }
  await writeFile(path.join(outputRoot, "reference.config.json"), `${JSON.stringify({ schemaVersion: 1, targets }, null, 2)}\n`);
}

async function main() {
  const version = (process.env.MAFIAHUB_SERVICES_CLI_VERSION || await readFile(path.join(apiRoot, "services-cli.version"), "utf8")).trim();
  if (!/^[0-9A-Za-z.+-]+$/.test(version)) throw new Error("Invalid services-cli version");
  const config = JSON.parse(await readFile(configPath, "utf8"));
  const originals = checkOnly
    ? await Promise.all(Object.values(config.targets).map((target) => readFile(path.join(apiRoot, target.generatedEntryPoint), "utf8")))
    : [];

  await mkdir(buildRoot, { recursive: true });
  runCli(version, ["docs", "check", "--config", configPath]);
  if (checkOnly) {
    const generated = await Promise.all(Object.values(config.targets).map((target) => readFile(path.join(apiRoot, target.generatedEntryPoint), "utf8")));
    if (generated.some((source, index) => source !== originals[index])) {
      throw new Error("Generated declarations changed; commit them with the binding metadata");
    }
    console.log("Mafia1Online scripting declarations are current");
    return;
  }

  const contractRoot = buildPath(option("--contract-out", "build/scripting-contract"));
  const siteRoot = buildPath(option("--site-out", "build/docs-site"));
  if (contractRoot === siteRoot || contractRoot.startsWith(`${siteRoot}${path.sep}`) || siteRoot.startsWith(`${contractRoot}${path.sep}`)) {
    throw new Error("Contract and site outputs must be separate directories");
  }
  await buildContract(config, contractRoot);

  const siteArgs = ["docs", "generate", "--config", configPath, "--out", siteRoot];
  const siteUrl = option("--site-url", "");
  if (siteUrl) siteArgs.push("--site-url", siteUrl);
  runCli(version, siteArgs);
  console.log(`Contract: ${contractRoot}`);
  console.log(`Documentation site: ${siteRoot}`);
}

main().catch((error) => {
  console.error(error instanceof Error ? error.message : error);
  process.exitCode = 1;
});
