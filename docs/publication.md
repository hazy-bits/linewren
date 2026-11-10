# Publication checklist

- The publication repository is [hazy-bits/linewren](https://github.com/hazy-bits/linewren).
  The first push starts with a snapshot of the reviewed source. The development
  history contains a personal author email and a local checkout path.
- Push the reviewed source and run **Build and test**. It runs the portable suite,
  compiles both guest smoke builds, and uploads the Mac OS 9 ZIP and the Tiger
  and Leopard DMGs.
  It does not run a vintage OS guest.
- Confirm the app license before the first public release.
- Test the three archives on the target systems and attach them to the first
  GitHub release. The same packaging command is available on a macOS host.

  ```sh
  ./scripts/package-release.sh release
  ```

- Enable GitHub Actions as the Pages source and run **Publish website** from the
  default branch. It serves `site/` directly. When the subdomain is confirmed,
  configure it in GitHub Pages and point its DNS CNAME at the organization’s
  Pages host. Enable HTTPS after GitHub verifies DNS.
- Add the project origin story when the author supplies it.

The app bundles no Apple SDK files. Retro68 provides the build toolchain.
The vendored smoke harness is a snapshot from ClassicMacAppTemplate at
`d435326c71be4ef74871d52c77fe21398a1459b2`. Its copy keeps this build usable
before the template gets a public remote. The local runner uses temporary
working directories for LaunchAPPL reports.
