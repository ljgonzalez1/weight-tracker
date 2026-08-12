# The platform layer

Everything in this document is about the three operating systems being genuinely
different, and about which call was chosen to hide that difference without
lying about it.

Five things need the platform, and only five. They are the whole of
`src/platform/`, plus one file-writing detail in `src/data/`:

| Question | Answered by |
|---|---|
| Where does the person's data live? | `platform/Workspace` |
| Is another copy already running? | `platform/SingleInstance` |
| Which language does this machine want? | `platform/SystemLocale` |
| How do I replace a file without losing it? | `data/AtomicFileWriter` |
| How many threads may I use? | `core/Concurrency` |

Nothing else in the codebase contains `#ifdef Q_OS_`. That is deliberate: the
number of places that can be wrong on one platform and right on another is kept
small enough to test exhaustively, and `tests/unit/test_platform_integration.cpp`
does exactly that.

---

## 1. Where the data lives

### The call that is actually used

`QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)`.

It is not a convenience wrapper over a hardcoded path. Underneath it is:

| Platform | Underlying API |
|---|---|
| Windows | `SHGetKnownFolderPath(FOLDERID_Documents, …)` |
| macOS | `NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, …)` |
| Linux, BSD | the XDG user-dirs configuration (`~/.config/user-dirs.dirs`) |

This matters more than it looks. On Windows, Documents can be redirected —
to another drive, to a network share, into OneDrive — and a great many programs
that hardcode `C:\Users\<name>\Documents` write to a folder the person does not
use. `SHGetKnownFolderPath` returns where it *is now*. The same is true of
iCloud Drive on macOS and of a localised Linux desktop that calls the folder
*Documentos*.

`FOLDERID_Documents` is also the reason no Win32 header appears anywhere in
this project: Qt already makes that call, and calling it a second time by hand
would add a platform-specific code path with no platform-specific benefit.

### The search order

`program_folder` is `weight`. The workspace is **looked for before it is
created**, in this order, and the first entry that exists and is writable wins:

**Linux and other Unixes**

1. `$XDG_DOCUMENTS_DIR/weight` — read from the environment, then from
   `~/.config/user-dirs.dirs` when it is not exported, which is the common case
   outside a desktop session
2. `DocumentsLocation/weight` — Qt's own resolution of the same thing
3. `$HOME/Documents/weight`
4. `$HOME/documents/weight`
5. `$HOME/Documentos/weight`
6. `$HOME/documentos/weight`
7. `$XDG_DATA_HOME/weight` — `~/.local/share/weight` when the variable is unset
8. `$HOME/.local/share/weight`

**Windows**

1. `DocumentsLocation\weight` — the known folder, wherever it currently points
2. `%USERPROFILE%\Documents\weight` — for the rare account where the known
   folder cannot be resolved
3. `%LOCALAPPDATA%\weight` — always exists, always writable, so the program can
   still start on a machine with no Documents folder at all

**macOS**

1. `DocumentsLocation/weight`
2. `~/Documents/weight`
3. `~/Documentos/weight`
4. `~/Library/Application Support/weight`

That last entry is `GenericDataLocation`, not `AppDataLocation`, and the
difference is not cosmetic: `AppDataLocation` on macOS already appends the
organisation and application names, so joining `weight` onto it would produce
`~/Library/Application Support/Weight/weight`. The generic form is
`~/Library/Application Support`, and the join gives the single, correctly named
folder.

### If nothing exists yet

The first candidate whose **parent** already exists is created. A fresh install
therefore lands in the real Documents folder rather than inventing a
`documentos` directory the person never had. If no parent exists either — a
brand-new account with an empty home — the last candidate is created, because
on every platform it sits under a directory the system guarantees.

The order is a list in one function. Changing the preference is changing the
order of that list; nothing else in the program knows it.

---

## 2. One instance at a time

### Why not a lock file

A lock file records an *intention*, not a *fact*. Kill the process with
`SIGKILL`, crash it, or lose power, and the file survives; every later run then
refuses to start until somebody deletes it by hand. Writing the pid into the
file and checking whether that pid is alive is better and still wrong, because
pids are recycled: the number in a stale file may now belong to somebody's text
editor.

### What is used instead

A **named local socket**, which the kernel owns rather than the filesystem.

| Platform | Mechanism | On abnormal death |
|---|---|---|
| Windows | named pipe | ceases to exist with the process — nothing to leave behind |
| Linux, macOS | Unix domain socket | the socket *file* may survive, but connecting to it fails immediately with `ConnectionRefused` |

So the liveness question is answered by **trying to talk to the other
instance**, not by inspecting a file. A dead instance cannot answer. When the
connection is refused, the stale entry is removed — which is safe precisely
because the refusal proved nobody is listening on it — and this process takes
the name.

The socket is created with `QLocalServer::UserAccessOption`, so one account
cannot block another on a shared machine.

### The guard runs on its own thread

The owner replies with its process id, which the second copy shows in small
type. That reply is delivered through a socket notifier, and a socket notifier
only fires while an event loop is spinning.

If the server lived on the main thread, then in any moment the main thread was
busy — a long fit, a modal dialog, a full-resolution export — a second copy
would be told "already running" with no process id, because the owner never got
round to answering. The guard therefore has a thread of its own, with its own
event loop, which makes the answer independent of whatever the application is
doing. The `QLocalServer` is created, connected and set listening *on that
thread*, because a `QLocalServer` binds its notifier to whichever thread calls
`listen()`.

`tests/unit/test_platform_integration.cpp` asserts this by acquiring the name
and then attempting a second acquisition in the same process with no event loop
running: it must be refused, and it must report this process's own pid. Before
the guard had its own thread, that test failed with a pid of 0.

### If the mechanism itself fails

An exhausted file-descriptor table, a read-only runtime directory: the person
is let through with a warning. Refusing to start because the duplicate-check
could not be set up would be a worse failure than the duplicate it prevents.

---

## 3. Replacing a file without losing it

`data/AtomicFileWriter` writes to a sibling temporary file, flushes it, and
renames it over the target. The rename is the atomic step: at no instant does a
reader see a half-written history.

Windows differs here and the difference is not optional. `MoveFileEx` will not
rename over an open file, so the implementation removes the target first, which
opens a window in which neither file exists. That is why the writer keeps the
temporary until the rename is confirmed, and why
`atomicWriteLeavesNoTemporaryBehind` in the platform suite writes five times in
a row and then asserts that exactly one file remains in the directory: a botched
sequence leaves `.tmp` siblings accumulating, and it does so silently.

Contents are re-read and compared after committing. It costs one extra read and
catches the case where a device reports success and discards the data — worth
paying for on a file that holds irreplaceable history.

---

## 4. How many threads

The pool is sized from the **work**, not from the cores:

```
threads = max(minimumThreads, curveCount + auxiliaryThreads)
```

Deliberately independent of `QThread::idealThreadCount()`. A two-thread machine
still gets one task per curve, and the kernel does the multiplexing — which is
what the operating system's scheduler is for. Twenty curves on a laptop
produces twenty-three threads, and the laptop copes, because they are mostly
waiting rather than computing.

`threadPoolExceedsTheHardwareCount` in the platform suite pins this down, so a
future "optimisation" that clamps to the hardware count fails loudly instead of
quietly serialising the fits.

---

## 5. Which language the machine wants

### The calls that are actually used

Unlike the four questions above, this one has no single Qt call that answers it
correctly on all three systems, so each is asked directly:

| Platform | API | What it returns |
|---|---|---|
| Windows | `GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, …)` | the ordered list behind Settings → Time & Language → Language, as BCP-47 names (`es-CL`, `en-GB`) |
| macOS | `CFLocaleCopyPreferredLanguages()` | the ordered list behind System Settings → General → Language & Region |
| POSIX | `setlocale(LC_MESSAGES, "")` | the C library's own resolution of `LC_ALL` / `LC_MESSAGES` / `LANG` |

with `QLocale::system().uiLanguages()` appended everywhere as a last resort, so
a platform none of these branches knows about still gets an answer.

### Why not `QLocale::system()` alone

Because it answers a slightly different question. `QLocale::system()` reports
the **formatting** locale — how to print a date, a number, a currency — and
what selects the language of an interface is the **UI language** list. On both
Windows and macOS the two genuinely differ, and the configuration where they
differ is not exotic: someone in Chile who reads English has Windows formatting
dates as `es-CL` and displaying its menus in `en-US`. Reading the UI language
list is what makes this program agree with every other program on that machine.

### Why a list rather than a value

Both Windows and macOS let the user *rank* languages, and that ranking is the
whole point of the feature. A machine ordered `de, es, en` is saying "German if
you have it, otherwise Spanish, otherwise English". Taking only the first entry
would answer English — the fallback — for a person who explicitly asked for
Spanish ahead of it. So the list is walked, and the first entry naming a
language the program actually ships wins.

A neutral tag (`C`, `POSIX`) stops the walk instead of being skipped. It is not
a language the program lacks; it is the explicit statement that no locale is
configured, and looking past an explicit statement in order to guess would be
worse than honouring it.

### The one non-obvious rule

`LANGUAGE` outranks `LC_ALL`, `LC_MESSAGES` and `LANG` — except when the
resolved locale is `C` or `POSIX`, where it is ignored entirely. That is GNU
gettext's rule, not an invention here, and it exists so that `LC_ALL=C` remains
a reliable way to get untranslated output. Every script that parses a program's
output depends on that, so the exception is implemented rather than tidied
away.

### Restoring what was borrowed

`setlocale(LC_MESSAGES, "")` changes process state, so the previous value is
read first and put back before the function returns. Only `LC_MESSAGES` is ever
touched, never `LC_NUMERIC`: a program that quietly switches its own decimal
separator is a program that writes `1,5` into a file another tool will read as
fifteen.

### Where it is tested

`tests/unit/test_localisation.cpp` covers the tag mapping exhaustively —
fifteen Spanish forms, five English ones, the unsupported languages, and the
two traps a prefix test falls into (`est_EE`, `eo`) — plus the environment
chain, including the `LC_ALL=C` exception. `--self-test` re-checks the mapping
against the binary that was actually shipped.

---

## 6. What each platform gets at install time

| | Linux | macOS | Windows |
|---|---|---|---|
| Command | `/usr/bin/weight` | `/usr/local/bin/weight` → bundle | `%LOCALAPPDATA%\Programs\Weight` on PATH |
| Application entry | `.desktop` + icon theme | `Weight.app` in `/Applications` | Start menu + desktop shortcut |
| Privileges | `sudo` | `sudo` | none |
| Package | `.deb` | `.dmg` | folder + `.zip` |

On Linux the `.desktop` file and the icon theme are what make the taskbar icon
appear **under Wayland**. A Wayland shell does not use the window icon at all:
it matches the surface to a desktop entry by name and takes the icon from
there. That is why `setWindowIcon` alone leaves the generic placeholder, and why
`QGuiApplication::setDesktopFileName` is called and a matching `.desktop` file
installed. Icons are installed at each size from the matching source file rather
than from one oversized PNG, so the shell picks a correctly rasterised image
instead of downscaling 512×512 to 16×16.

On macOS `sudo make install` also creates a symlink from `bin/weight` into the
bundle, so `weight` works from a terminal. A symlink rather than a copy means
there is one binary: updating the app updates the command.

`make uninstall` removes exactly what was installed and never touches the
workspace folder. Neither does `dpkg --purge`.
