/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
using System;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Windows.Forms;

[assembly: AssemblyTitle("mGBA Willow")]
[assembly: AssemblyProduct("mGBA Willow")]

internal static class WillowStartup {
    [STAThread]
    private static int Main(string[] args) {
        try {
            if (args.Length != 1 || !Path.IsPathRooted(args[0]) || !File.Exists(args[0])) {
                throw new InvalidOperationException("The Willow launcher is missing. Reinstall startup from Launch mGBA.cmd.");
            }

            string launcher = args[0];
            string checkout = Path.GetDirectoryName(Path.GetDirectoryName(launcher));
            string powershell = Path.Combine(Environment.SystemDirectory, @"WindowsPowerShell\v1.0\powershell.exe");
            var startInfo = new ProcessStartInfo(powershell) {
                Arguments = "-NoLogo -NoProfile -ExecutionPolicy Bypass -File \"" + launcher + "\" -Background",
                WorkingDirectory = checkout,
                UseShellExecute = false,
                CreateNoWindow = true
            };
            using (Process process = Process.Start(startInfo)) {
                process.WaitForExit();
                if (process.ExitCode != 0) {
                    throw new InvalidOperationException("Willow could not start. See the launcher log at " +
                        Path.Combine(checkout, @"build-auto-windows\launcher.log"));
                }
            }
            return 0;
        } catch (Exception error) {
            MessageBox.Show(error.Message, "mGBA Willow", MessageBoxButtons.OK, MessageBoxIcon.Error);
            return 1;
        }
    }
}
