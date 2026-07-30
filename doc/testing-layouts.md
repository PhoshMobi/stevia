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

[resource overlays]: https://docs.gtk.org/gio/struct.Resource.html#overlays
