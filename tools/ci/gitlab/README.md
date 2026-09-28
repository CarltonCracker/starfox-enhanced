# GitLab release builds

`.gitlab-ci.yml` replaces the portable-builds GitHub Actions workflow. It
builds Windows x64/x86, PCVR, Linux, macOS, iOS, Android, Quest, Switch and
Vita on separate clean runners and stores packages as GitLab job artifacts.
The Xbox UWP job requires a Windows runner with the UWP workload and VCLibs;
it is manual until such a runner is registered with tag
`starfox-windows-uwp`. No ROMs or private asset bundles are packaged.

For 0.0.8, run a pipeline on the CI branch with `CI_BUILD_SET=desktop` first.
After it passes, use `mobile`, `quest`, or `all` as needed. Set
`CI_RELEASE_TAG=v0.0.8`; `CI_PUBLISH_RELEASE=true` publishes the selected
verified packages and a SHA256 manifest to that existing tag's GitLab release.
The release script refuses missing packages for the selected build set, and
the default `smoke` pipeline only validates Linux without publishing.

For official Android/Quest release packages, set masked, protected GitLab
CI/CD variables `ANDROID_RELEASE_KEYSTORE` (base64 PKCS#12) and
`ANDROID_RELEASE_PASSWORD` (password for that keystore). They must be the
original release signing credentials; a temporary validation key is generated
only for non-publishing pipelines. If the original key cannot be recovered,
Android/Quest upgrades will require a separate signing-key migration plan.

The hosted macOS runner uses the GitLab macOS runner offering; if it is not
enabled for this project, register a compatible macOS runner with the tag in
`.gitlab-ci.yml`. Build jobs do not publish a release on their own. A new
version needs a matching `docs/RELEASE-X.Y.Z.md` file and `vX.Y.Z` tag.
