# Stevia

Stevia is an alternative keyboard for Phosh. It can replace the default OSK
[squeekboard][].

The purpose of Stevia is:

- to make typing pleasant and fast on touch screens
- be helpful when debugging input-method related issues
- be quick and easy to (cross)compile
- to be easy to extend (hence the API documentation)

Features:

- easy to swap out with squeekboard (implements phosh's [sm.puri.OSK0]() DBus
  interface) for low risk experimentation
- easy to temporarily replace running instance (`--replace` option)
- no language boundaries within the codebase to lower the entrance barrier
- use current GTK and GObject patterns (actions, bindings, …)
- use GNOME libs and technologies wherever possible (GSettings, json-glib, …)
- [character popover](https://gitlab.gnome.org/World/Phosh/stevia/-/raw/main/screenshots/pos-popover.png)
- [emoji layout](https://gitlab.gnome.org/World/Phosh/stevia/-/raw/main/screenshots/pos-emoji.png)
- cursor navigation via space-bar long-press
- word correction via hunspell
- use any program as completer via a `pipe` completer ([Example](https://social.librem.one/@agx/110260534404795348))
- [word completion](https://social.librem.one/@agx/109428599061094716)
  based on the presage library
- experimental input of Indic languages using [varnam](https://github.com/varnamproject)
- allow for secondary completers to amend completion results
- allow to prevent keyboard unfold for certain apps (via app-id)
- allow to prevent keyboard unfold when a hardware keyboard is present

## License

Stevia is licensed under the GPLv3+.

## Getting the source

```sh
git clone https://gitlab.gnome.org/World/Phosh/stevia
cd stevia
```

The [main][] branch has the current development version.

## Dependencies

On a Debian based system run

```sh
sudo apt-get -y install build-essential
sudo apt-get -y build-dep .
```

For an explicit list of dependencies check the `Build-Depends` entry in the
[debian/control][] file.

## Building

We use the meson (and thereby Ninja) build system for phosh-osk-stevia.
The quickest way to get going is to do the following:

```sh
meson setup -Dgtk_doc=false _build
meson compile -C _build
meson test -C _build
```

We're disabling the doc build above as it reduces build time a lot.

## Running

### Running from the source tree

When running from the source tree first start *[phosh][]*.
Then start *phosh-osk-stevia* using:

```sh
_build/run --replace
```

Note that there's no need to install any files outside the source tree. The
`--replace` option *temporarily* replaces a running `phosh-osk-stevia` so there's
no need to stop a running instance.

The result should look something like this:

![character popover](screenshots/pos-popover.png)
![emoji layout](screenshots/pos-emoji.png)
![inscript/malayalam](screenshots/pos-wide-in+mal.png)

## Word completion

``phosh-osk-stevia`` has support for word completion. There are different
completers built in and it's easy to add more. See the [manpage][] on
available completers and how to configure them, see also [this blog
post](https://phosh.mobi/posts/osk-completion/) for more details.

## Documentation

### Development Documentation

The API and development documentation is available at
<https://world.pages.gitlab.gnome.org/Phosh/stevia>. See also the OSK
related posts on [phosh.mobi](https://phosh.mobi/tags/osk/).

### End User Documentation

For end user documentation see the [manpage][] (or in the installed
system via `man phosh-osk-stevia`).

## Getting in Touch

- Issue tracker: <https://gitlab.gnome.org/World/Phosh/stevia/issues>
- Matrix: <https://matrix.to/#/#phosh:sigxcpu.org>

[main]: https://gitlab.gnome.org/World/Phosh/stevia/-/tree/main
[.gitlab-ci.yml]: https://gitlab.gnome.org/World/Phosh/stevia/-/blob/main/.gitlab-ci.yml
[debian/control]:https://gitlab.gnome.org/World/Phosh/stevia/-/blob/main/debian/control
[phosh]: https://gitlab.gnome.org/World/Phosh/phosh
[squeekboard]: https://gitlab.gnome.org/World/Phosh/squeekboard
[sm.puri.OSK0]: https://gitlab.gnome.org/World/Phosh/phosh/-/blob/main/src/dbus/sm.puri.OSK0.xml
[phosh-osk-data]: https://gitlab.gnome.org/guidog/phosh-osk-data/
[manpage]: ./doc/phosh-osk-stevia.rst
