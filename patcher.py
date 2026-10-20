# -*- coding: utf-8 -*-
# Nuked SC-55 P256 Patcher (issue #44): a window around p256_builder.py for Windows, Linux and macOS.
# Releases freeze this file with PyInstaller into one executable per system, with the ROM-less
# templates inside (.github/workflows/build.yml). It calls the same functions as p256_builder.py,
# so both always produce byte-identical plugins.
# * ROM files are searched in the patcher's folder (subfolders included) by SHA-256, or in a
#   folder the user chooses. 'Patch' builds every complete model into the folder 'Plugins' next
#   to the patcher (Plugins/CLAP, Plugins/VST2).
# * patcher --build [folder]: same without a window (prints like p256_builder.py).
# * patcher --selftest: checks the bundled templates (used by the release workflow).
# * Language: the system language, else English; a menu at the top right switches it.
import sys, os, threading, queue, subprocess, locale
from pathlib import Path

FROZEN = getattr(sys, 'frozen', False)
HERE = Path(sys.executable).resolve().parent if FROZEN else Path(__file__).resolve().parent
if FROZEN and sys.platform == 'darwin' and HERE.parent.name == 'Contents':
    HERE = HERE.parents[2]                      # Patcher.app/Contents/MacOS -> folder holding the .app
CODE = Path(getattr(sys, '_MEIPASS', Path(__file__).resolve().parent))
sys.path.insert(0, str(CODE))
import p256_builder as builder                  # noqa: E402
try:
    from patcher_version import VERSION
except ImportError:
    VERSION = 'dev'

TEMPLATES = CODE / 'templates'
ICONS = CODE / 'icons'
ICON_FILES = ('patcher_16.png', 'patcher_32.png', 'patcher.png')   # window icon (Tk picks the best size)
PLUGINS = HERE / 'Plugins'
NAME = 'Nuked SC-55 P256 Patcher'


def system_language(known) -> str:
    names = []
    if sys.platform == 'win32':
        try:
            import ctypes
            names.append(locale.windows_locale.get(ctypes.windll.kernel32.GetUserDefaultUILanguage(), ''))
        except Exception:
            pass
    elif sys.platform == 'darwin':
        try:
            out = subprocess.run(['defaults', 'read', '-g', 'AppleLanguages'], capture_output=True,
                                 text=True, timeout=3).stdout
            names += [x.strip(' "(),\n') for x in out.split('\n') if x.strip(' "(),\n')]
        except Exception:
            pass
    for var in ('LC_ALL', 'LC_MESSAGES', 'LANG', 'LANGUAGE'):
        if os.environ.get(var):
            names.append(os.environ[var])
    try:
        names.append(locale.getlocale()[0] or '')
    except Exception:
        pass
    alias = {'iw': 'he', 'in': 'id', 'ji': 'yi'}          # old ISO codes still used by some systems
    for n in names:
        code = n.replace('-', '_').split('_')[0].split('.')[0].lower()
        code = alias.get(code, code)
        if code in known:
            return code
    return 'en'


def scan(folder: Path):
    """Model list (p256_builder.plan) for the ROM files below folder."""
    skip = [PLUGINS]
    if not FROZEN:                              # running from the repository: skip its own files
        skip += [CODE / 'templates', CODE / 'src', CODE / '.git']
    return builder.plan(builder.scan_roms(folder, skip))


def build(items, log):
    """Builds the complete models; log(text). Returns the titles that failed."""
    failed = []
    for item in items:
        try:
            builder.build_model(item, TEMPLATES, PLUGINS)
        except Exception as e:                  # also unexpected errors: the patcher must not freeze
            log(f"[!!] {item['title']}: {e if isinstance(e, (ValueError, OSError)) else f'{type(e).__name__}: {e}'}\n")
            failed.append(item['title'])
            continue
        note = ' (CTF patch applied)' if item['ctf'] else ''
        log(f"[ok] {item['title']}{note}: Plugins/CLAP/{item['file']}.clap, Plugins/VST2/{item['file']}.dll\n")
        for _, role in item['parts']:
            log(f"       {role:8} <- {item['have'][role]}\n")
    return failed


def run_build(folder: Path) -> int:
    print(f'{NAME} {VERSION}\n')
    items = scan(folder)
    for it in items:
        if it['missing'] and any(it['have'].values()):
            print(f"[--] {it['title']}: incomplete, missing: {', '.join(it['missing'])}")
        elif it['missing']:
            print(f"[--] {it['title']}: no ROMs found")
    done = [it for it in items if not it['missing']]
    failed = build(done, lambda t: print(t, end=''))
    n = len(done) - len(failed)
    print(f'\n{n} plugin(s) created in {PLUGINS}.' if n else '\nNo plugin created.')
    return 0 if n and not failed else 2


def selftest() -> int:
    for _, name, title, parts in builder.MODELS:
        tpl = TEMPLATES / f'{name}.p256tpl'
        assert tpl.is_file(), f'template missing: {tpl}'
        data = builder.fill_template(tpl.read_bytes(), [(loc, b'\0' * 64) for loc, _ in parts], title)
        assert data.count(builder.MAGIC) == 1 and data[:2] == b'MZ', f'template broken: {tpl}'
    assert len(builder.apply_ctf(bytes(524288))) == 524288
    for f in ICON_FILES:
        assert (ICONS / f).is_file(), f'icon missing: {ICONS / f}'
    print('selftest ok', VERSION)
    return 0


RTL = {'ar', 'fa', 'he'}


def shape_rtl(text: str, width: int = 0) -> str:
    """Tk on Linux (Xft) neither joins Arabic letters nor reorders right-to-left text; Windows and
    macOS do both themselves. On Linux the text is prepared with arabic_reshaper and python-bidi
    (bundled in the release build); without them it is shown unchanged. Long texts are wrapped
    here (width = characters per line) before reordering, since Tk would wrap the reordered text
    from the wrong end."""
    if sys.platform in ('win32', 'darwin'):
        return text
    try:
        import arabic_reshaper
        from bidi.algorithm import get_display
    except ImportError:
        return text
    import textwrap
    lines = textwrap.wrap(text, width) if width else [text]
    return '\n'.join(get_display(arabic_reshaper.reshape(line)) for line in lines)


def main_window():
    import tkinter as tk
    from tkinter import ttk, filedialog
    from patcher_i18n import TEXT, LANGUAGES

    root = tk.Tk()
    root.title(f'{NAME} {VERSION}')
    root.resizable(False, False)
    try:
        root._icons = [tk.PhotoImage(file=str(ICONS / f)) for f in ICON_FILES]   # keep references
        root.iconphoto(True, *root._icons)
    except Exception:                       # a missing or unreadable icon must never stop the patcher
        pass
    st = dict(lang=system_language(LANGUAGES), folder=HERE, items=[], busy=False, details=False,
              status=('scanning', {}), asked=False)
    q = queue.Queue()

    def T(key, width=0, **kw):
        text = TEXT[st['lang']].get(key, TEXT['en'][key]).format(**kw)
        return shape_rtl(text, width) if st['lang'] in RTL else text

    shown = {code: shape_rtl(name) if code in RTL else name for code, name in LANGUAGES.items()}

    frame = ttk.Frame(root, padding=14)
    frame.grid(sticky='nsew')
    head = ttk.Frame(frame)
    head.grid(row=0, column=0, columnspan=3, sticky='ew')
    ttk.Label(head, text=NAME, font=('TkDefaultFont', 13, 'bold')).pack(side='left')
    lang_var = tk.StringVar(value=shown[st['lang']])
    lang_box = ttk.Combobox(head, textvariable=lang_var, values=list(shown.values()),
                            state='readonly', width=16, height=len(LANGUAGES))
    lang_box.pack(side='right')
    intro = ttk.Label(frame, wraplength=520)
    intro.grid(row=1, column=0, columnspan=3, sticky='w', pady=(2, 10))
    box = ttk.LabelFrame(frame, padding=10)
    box.grid(row=2, column=0, columnspan=3, sticky='ew')
    box.columnconfigure(1, weight=1)
    folder_lbl = ttk.Label(box)
    folder_lbl.grid(row=0, column=0, sticky='w')
    folder_val = ttk.Label(box, foreground='#444')
    folder_val.grid(row=0, column=1, sticky='w', padx=6)
    browse = ttk.Button(box)
    browse.grid(row=0, column=2, sticky='e')
    rows = ttk.Frame(box)
    rows.grid(row=1, column=0, columnspan=3, sticky='ew', pady=(8, 0))
    rows.columnconfigure(1, weight=1)
    target = ttk.Label(frame, foreground='#444')
    target.grid(row=3, column=0, columnspan=3, sticky='w', pady=(8, 0))
    progress = ttk.Progressbar(frame, mode='determinate')
    progress.grid(row=4, column=0, columnspan=3, sticky='ew', pady=(8, 0))
    ttk.Separator(frame).grid(row=5, column=0, columnspan=3, sticky='ew', pady=10)
    status = ttk.Label(frame, wraplength=330)
    status.grid(row=6, column=0, sticky='w')
    det_btn = ttk.Button(frame)
    det_btn.grid(row=6, column=1, sticky='e', padx=6)
    patch_btn = ttk.Button(frame, width=14)
    patch_btn.grid(row=6, column=2, sticky='e')
    open_btn = ttk.Button(frame)
    log = tk.Text(frame, height=14, width=80, font=('TkFixedFont', 9), state='disabled')
    frame.columnconfigure(0, weight=1)

    def ready_items():
        return [it for it in st['items'] if not it['missing']]

    def refresh():
        intro['text'] = T('intro', 70)
        box['text'] = T('files')
        folder_lbl['text'] = T('folder') + ':'
        folder_val['text'] = str(st['folder'])
        browse['text'] = T('browse')
        for w in rows.winfo_children():
            w.destroy()
        for i, it in enumerate(st['items']):
            if not it['missing']:
                mark, color, text = '✔', '#1a7f37', T('complete_ctf') if it['ctf'] else T('complete')
            elif any(it['have'].values()):
                mark, color, text = '✘', '#c62828', T('missing', roles=', '.join(it['missing']))
            else:
                mark, color, text = '–', '#888', T('none')
            ttk.Label(rows, text=mark, foreground=color, width=2).grid(row=i, column=0, sticky='w')
            ttk.Label(rows, text=it['title']).grid(row=i, column=1, sticky='w', padx=6)
            ttk.Label(rows, text=text, foreground='#555').grid(row=i, column=2, sticky='e')
        target['text'] = T('target') + ':  ' + str(PLUGINS)
        key, kw = st['status']
        status['text'] = T(key, 40, **kw)
        det_btn['text'] = T('hide') if st['details'] else T('details')
        patch_btn['text'] = T('patch')
        open_btn['text'] = T('open_folder')
        patch_btn.state(['!disabled'] if not st['busy'] and ready_items() else ['disabled'])
        browse.state(['disabled'] if st['busy'] else ['!disabled'])

    def set_status(key, **kw):
        st['status'] = (key, kw)
        refresh()

    def write_log(text):
        log.configure(state='normal')
        log.insert('end', text)
        log.see('end')
        log.configure(state='disabled')

    def start_scan():
        st['busy'] = True
        set_status('scanning')
        progress.configure(mode='indeterminate')
        progress.start(12)

        def work():
            try:
                q.put(('scanned', scan(st['folder'])))
            except Exception as e:
                q.put(('line', f'{e}\n'))
                q.put(('scanned', []))
        threading.Thread(target=work, daemon=True).start()

    def scanned(items):
        progress.stop()
        progress.configure(mode='determinate', value=0)
        st.update(busy=False, items=items)
        n = len(ready_items())
        if n:
            set_status('ready', n=n)
        else:
            set_status('need_roms' if st['folder'] == HERE else 'no_roms')
        if not n and st['folder'] == HERE and not st['asked']:
            st['asked'] = True
            root.after(200, choose_folder)

    def choose_folder():
        d = filedialog.askdirectory(title=T('pick_folder'), initialdir=str(st['folder']))
        if d:
            st['folder'] = Path(d)
            start_scan()

    def toggle_details():
        st['details'] = not st['details']
        if st['details']:
            log.grid(row=8, column=0, columnspan=3, sticky='nsew', pady=(10, 0))
        else:
            log.grid_remove()
        refresh()

    def start_patch():
        items = ready_items()
        st['busy'] = True
        open_btn.grid_remove()
        progress.configure(maximum=len(items), value=0)

        def work():
            failed = []
            try:
                for i, it in enumerate(items):
                    q.put(('step', i, len(items), it['title']))
                    failed += build([it], lambda t: q.put(('line', t)))
            except Exception as e:              # never leave the window in the "patching" state
                q.put(('line', f'[!!] {type(e).__name__}: {e}\n'))
                failed += [it['title'] for it in items if it['title'] not in failed][:1] or ['?']
            finally:
                q.put(('finished', len(items) - len(failed), failed))
        threading.Thread(target=work, daemon=True).start()

    def step(i, total, title):
        progress.configure(value=i)
        set_status('patching', model=title, i=i + 1, total=total)

    def finished(n, failed):
        st['busy'] = False
        progress.configure(value=progress['maximum'])
        if failed:
            set_status('failed', models=', '.join(failed))
            if not st['details']:
                toggle_details()
        else:
            set_status('done', n=n)
        open_btn.grid(row=7, column=2, sticky='e', pady=(8, 0))

    def open_folder():
        p = str(PLUGINS)
        if sys.platform == 'win32':
            os.startfile(p)
        elif sys.platform == 'darwin':
            subprocess.Popen(['open', p])
        else:
            subprocess.Popen(['xdg-open', p])

    def pick_lang(_=None):
        st['lang'] = {v: k for k, v in shown.items()}[lang_var.get()]
        refresh()

    def poll():
        try:
            while True:
                m = q.get_nowait()
                {'scanned': scanned, 'line': write_log, 'step': step, 'finished': finished}[m[0]](*m[1:])
        except queue.Empty:
            pass
        root.after(80, poll)

    browse['command'] = choose_folder
    det_btn['command'] = toggle_details
    patch_btn['command'] = start_patch
    open_btn['command'] = open_folder
    lang_box.bind('<<ComboboxSelected>>', pick_lang)
    refresh()
    start_scan()
    poll()
    root.mainloop()


if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] == '--selftest':
        sys.exit(selftest())
    elif len(sys.argv) > 1 and sys.argv[1] == '--build':
        sys.exit(run_build(Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else HERE))
    else:
        main_window()
