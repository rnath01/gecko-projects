/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const {Cu, Cc, Ci} = require("chrome");

const promise = require("promise");
const { Task } = Cu.import("resource://gre/modules/Task.jsm", {});
const { OS }  = Cu.import("resource://gre/modules/osfile.jsm", {});
const Subprocess = require("sdk/system/child_process/subprocess");

const Git = module.exports = {
  clone: Task.async(function* (directory, repo, changeset) {
    yield this._exec("git", ["clone", repo, "."], directory);
    if (changeset) {
      yield this._exec("git", ["checkout", changeset], directory);
    }
  }),

  // If the app depends on some build step, run it before pushing the app
  _exec: Task.async(function* (command, command_args, cwd) {
    //let envService = Cc["@mozilla.org/process/environment;1"].getService(Ci.nsIEnvironment);
    //let home = envService.get("HOME");

    // Run the command through a shell command in order to support non absolute
    // paths.
    // On Windows `ComSpec` env variable is going to refer to cmd.exe,
    // Otherwise, on Linux and Mac, SHELL env variable should refer to
    // the user chosen shell program.
    // (We do not check for OS, as on windows, with cygwin, ComSpec isn't set)
    let envService = Cc["@mozilla.org/process/environment;1"].getService(Ci.nsIEnvironment);
    let shell = envService.get("ComSpec") || envService.get("SHELL");

    let args = []
    // For cmd.exe, we have to pass the `/C` option,
    // but for unix shells we need -c.
    // That to interpret next argument as a shell command.
    if (envService.exists("ComSpec")) {
      args.push("/C");
    } else {
      args.push("-c");
    }

    args.push(command + " " + command_args.map(a => a ? ("\"" + a.replace(/"/g,"\\\"") + "\"") : "").join(" "));

    // Subprocess changes CWD, we have to save and restore it.
    let originalCwd = yield OS.File.getCurrentDirectory();
    try {
      let defer = promise.defer();
      Subprocess.call({
        command: shell,
        arguments: args,
        workdir: cwd,

        stdout: data =>
          console.log("git: " + data),
        stderr: data =>
          console.error("git: " + data),

        done: result => {
          console.log("git: Terminated with error code: " + result.exitCode);
          if (result.exitCode == 0) {
            defer.resolve();
          } else {
            defer.reject("git command failed with error code " + result.exitCode);
          }
        }
      });
      defer.promise.then(() => {
        OS.File.setCurrentDirectory(originalCwd);
      });
      yield defer.promise;
    } catch (e) {
      throw new Error("Unable to run git command '" + command + "' " +
                      shell + " " + args.join(" ") + ":\n" + (e.message || e));
    }
  }),
};

