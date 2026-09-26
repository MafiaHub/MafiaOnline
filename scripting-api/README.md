# Mafia Online scripting contract

The C++ bindings in `code/server` and `code/client` define the scripting API.
Their v8pp metadata is captured in `generated/server-api.json` and
`generated/client-api.json`. Debug runtimes refresh these files during server
startup and client scripting initialization.

Run the contract generator from this project directory after updating the
metadata:

```sh
node scripts/build_scripting_docs.mjs
node scripts/build_scripting_docs.mjs --check
```

The first command validates the binding catalog, regenerates the declarations,
and writes a contract bundle to `build/scripting-contract`. The second command
checks that the committed declarations match the metadata. The generated
`*.d.ts` files and JSON metadata are committed so API changes can be reviewed.

The [Mafia Online Docs](https://github.com/MafiaHub/MafiaOnlineDocs) repository
combines this contract with authored guides. Build it locally by setting
`MAFIAONLINE_CONTRACT_ROOT` to this project's `build/scripting-contract`.

`server-api.d.ts` and `client-api.d.ts` remain editor declarations for game
resources. Server scripts use Node.js; client scripts run in sandboxed V8.
