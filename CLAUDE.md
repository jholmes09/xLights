# xLights, Jeff Holmes Presents fork

This fork exists so the customized xLights build Jeff ships has published source. xLights is GPLv3.

- `master` mirrors upstream xLightsSequencer/xLights. Never commit to it.
- `jhp` is exactly one upstream release tag plus one JHP commit: IP output socket recovery, Pixlite16, LayoutPanel.
- To take an upstream update: fetch upstream with tags, rebase `jhp` onto the new release tag, report what conflicted, and stop. Never cherry-pick upstream commits onto an older base.
- Track stable release tags while a show is running. Nightly and master are for the off season.
- Never build in CI and never add a workflow. Builds happen on Jeff's Mac from a checkout of `jhp`.
- Branch `jhp-patch-import` is the archive of how `jhp` was seeded. Leave it.
