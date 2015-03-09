/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

/**
 * We skip authenticode cert checks from the service udpates
 * so that we can use updater-xpcshell with the wrong certs for testing.
 * This tests that code path.  */

function run_test() {
  if (!IS_AUTHENTICODE_CHECK_ENABLED) {
    return;
  }

  let binDir = getGREBinDir();
  let maintenanceServiceBin = binDir.clone();
  maintenanceServiceBin.append(FILE_MAINTENANCE_SERVICE_BIN);

  let updaterBin = binDir.clone();
  updaterBin.append(FILE_UPDATER_BIN);

  logTestInfo("Launching maintenance service bin: " +
              maintenanceServiceBin.path + " to check updater: " +
              updaterBin.path + " signature.");

  // Bypass the manifest and run as invoker
  let env = AUS_Cc["@mozilla.org/process/environment;1"].
            getService(AUS_Ci.nsIEnvironment);
  env.set("__COMPAT_LAYER", "RunAsInvoker");

  let dummyInstallPath = "---";
  let maintenanceServiceBinArgs = ["check-cert", dummyInstallPath,
                                   updaterBin.path];
  let maintenanceServiceBinProcess = AUS_Cc["@mozilla.org/process/util;1"].
                                     createInstance(AUS_Ci.nsIProcess);
  maintenanceServiceBinProcess.init(maintenanceServiceBin);
  maintenanceServiceBinProcess.run(true, maintenanceServiceBinArgs,
                                   maintenanceServiceBinArgs.length);
  do_check_eq(maintenanceServiceBinProcess.exitValue, 0);
}
