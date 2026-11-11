# Publication checklist

- The publication repository is [hazy-bits/linewren](https://github.com/hazy-bits/linewren).
  The first push starts with a snapshot of the reviewed source. The development
  history contains a personal author email and a local checkout path.
- Set the next release number in `VERSION` and push the changes to `main`.
  **Build and test** runs the portable tests, compiles both guest smoke builds,
  and uploads the Mac OS 9 ZIP and the Tiger and Leopard DMGs. Untagged CI builds
  add `-dev.<run>.<attempt>` to the version in the app and archive names.
- When the commit is ready to release, tag it with `v` followed by its
  `VERSION`. For the first release, use `v0.2.0`.

  ```sh
  git fetch origin main
  git tag -a v0.2.0 origin/main -m "Linewren 0.2.0"
  git push origin v0.2.0
  ```

  GitHub Actions checks the tag, builds all three archives with the release
  version, and attaches them to a draft GitHub release. Review the release
  notes and test the archives on the target systems before publishing the draft.
- To build the same archives locally on macOS, run the packaging script from
  the repository root.

  ```sh
  ./scripts/package-release.sh release
  ```

- Enable GitHub Actions as the Pages source. **Publish website** serves `site/`
  directly after changes to the site or its workflow on the default branch.
  It can also run manually. When the subdomain is confirmed,
  configure it in GitHub Pages and point its DNS CNAME at the organization’s
  Pages host. Enable HTTPS after GitHub verifies DNS.
- Add the project origin story when the author supplies it.

The app bundles no Apple SDK files. Retro68 provides the build toolchain.
The flake lock pins the [Classic Mac App Template](https://github.com/hazy-bits/classic-mac-app-template)
used for cross-builds and guest smoke tests.
