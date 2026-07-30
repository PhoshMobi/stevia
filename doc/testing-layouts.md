Title: Testing Keyboard Layouts
Slug: testinglayouts

Testing layout changes or experimenting with new layouts is simple due
to GLib's [resource overlays][]. This mechanism allows you to replace
or add files to Stevia's built-in resource bundle without rebuilding
stevia.

Let's assume you have the layout's JSON in the current directory
in a file named `de.json`. You can then use that layout by running:

```sh
G_RESOURCE_OVERLAYS=/mobi/phosh/stevia/layouts=$PWD phosh-osk-stevia --replace
```

This maps the current directory as an overlay for the resource path
`/mobi/phosh/stevia/layouts`, so de.json is made available as
`/mobi/phosh/stevia/layouts/de.json`. This replaces the existing
`de` layout, adding new layouts for other languages for testing works
the same way.

If everything works you should see this on the console:

```console
GLib-GIO-Message: Adding GResources overlay '/mobi/phosh/stevia/layouts=/path/to/current/dir'
GLib-GIO-Message: Mapped file '/path/to/current/dir/de.json' as a resource overlay
```

This informs you that Stevia picked up your modified layout (note that
`/path/to/current/dir/` depends on your current working directory so
the output might be slightly different).

If you need to make further changes just stop Stevia and run the above
command again. This allows for quick test cycles without risking to
break your system.

# Testing in a nested session

You can test these layouts in a nested session using Phoc. First start
phoc nested in the Wayland session:

```sh
WLR_BACKENDS=wayland /usr/bin/phoc GSETTINGS_BACKEND=memory -E kgx
```

Then in another terminal start Stevia against that nested Phoc:

```sh
gsettings set org.gnome.desktop.a11y.applications screen-keyboard-enabled true
WAYLAND_DISPLAY=wayland-1 POS_DEBUG=force-show phosh-osk-stevia --replace
```

You should now see Stevia in the nested Phoc window. The `force-show`
ensures that Stevia is shown even though no app requests it. `wayland-1`
is the socket created by Phoc, the socket name is printed by Phoc on
startup:

```console
Running compositor on wayland display 'wayland-1'
```

You can terminate Stevia at any time using `Ctrl-C` and pass options
like `G_RESOURCE_OVERLAYS` as shown above. You can also easily test
stevia against different apps by passing other values for `-E` like
`-E xterm` to test X11 interaction.

[resource overlays]: https://docs.gtk.org/gio/struct.Resource.html#overlays
[phoc]: https://gitlab.gnome.org/World/Phosh/phoc
