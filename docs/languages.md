# Languages / 语言

The application includes English (`en`), Simplified Chinese (`zh`), Hindi (`hi`), Spanish (`es`) and Modern Standard Arabic (`ar`). These five follow total-speaker usage (native plus additional-language speakers), rather than a ranking by native speakers alone. Regional variants such as `es_MX`, `zh_CN` and `ar_EG` select the corresponding catalog.

程序内置英语、简体中文、印地语、西班牙语、现代标准阿拉伯语，按母语及第二语言使用人数选择。地区变体会匹配对应语言。

```sh
linux-factory-reset gui                    # automatic / 自动
linux-factory-reset gui --language zh
linux-factory-reset gui --language hi
linux-factory-reset gui --language es
linux-factory-reset gui --language ar
linux-factory-reset --language en --help
linux-factory-reset check --language es    # read-only / 只读
linux-factory-reset languages              # machine-readable list / 语言清单
```

Use the last **Language / 语言** menu entry to change the interface immediately. Its choices show each language's own name. The preference is stored privately in `$XDG_CONFIG_HOME/linux-factory-reset/language` (otherwise `~/.config/linux-factory-reset/language`), with mode 0600. Choose automatic to follow the environment again. No system-wide language setting is changed.

在主界面最后一项“语言”里即可切换。选择会保存在当前用户的私有配置文件中，权限 0600；选择“跟随系统”恢复自动识别，不修改系统语言。

Priority: explicit `--language` > `LFR_LANGUAGE` > saved preference > `LANGUAGE`, `LC_ALL`, `LC_MESSAGES`, `LANG` > English. Unsupported system locales fall back to English; an unsupported explicit option is rejected. Root operations do not read user preferences; the GUI passes its selected language explicitly to the privileged backend.

优先级为命令参数、`LFR_LANGUAGE`、已保存选择、系统语言环境、英语。提权操作接收明确语言参数，不读取普通用户的偏好文件。

## Confirmation / 清空确认

Every language explains that restoration removes **all previous users, personal files, additional software and configuration**. The exact confirmation token is always:

所有语言均说明删除全部旧用户、个人文件、额外软件和配置。确认时统一输入以下 ASCII 字符，大小写必须一致：

```text
ERASE-ALL-USERS
```

Commands, JSON keys, stage codes, checksums, paths and this token are never translated. A translated phrase, partial token or other input cannot authorize deletion. The previous Chinese confirmation phrase is no longer accepted.

命令、JSON 字段、阶段代码、校验值和路径保持原样；翻译文字、部分字符或其他输入均不能代替确认口令。旧版中文确认词不再接受。

## Arabic and fonts / 阿拉伯语与字体

Arabic uses actual GTK right-to-left layout, including list columns and dialog alignment. DEBs require `locales`; Ubuntu packages also require `language-pack-gnome-ar`. RPMs require `glibc-langpack-ar`. GTK 3 also needs its Arabic `gtk30.mo` catalog (normally supplied by `libgtk-3-common`); custom/slim images that strip `/usr/share/locale` must preserve/reinstall it. On minimal Debian systems with locale sources but no generated Arabic locale, the GUI runs `localedef` once into its own user cache (`$XDG_CACHE_HOME/linux-factory-reset/locales`, otherwise `~/.cache/linux-factory-reset/locales`). It never edits `/etc/locale.gen`, the system locale or firmware. If that locale cannot be created/loaded, Arabic GUI startup reports the missing requirement rather than silently using the wrong direction.

阿拉伯语使用 GTK 实际的从右向左布局。DEB/RPM 自动声明相应语言依赖；精简 Debian 上可在当前用户缓存里生成阿拉伯语 locale，不修改系统默认语言。生成失败会明确报错。

DEBs recommend `fonts-noto-core` and `fonts-noto-cjk`. Install them if characters appear as boxes, especially when installing without recommendations. Standalone binaries embed all five catalogs but still need Zenity, GTK translations, locale data and suitable fonts from the distribution. Desktop font selection and scaling remain controlled by the OS.

DEB 推荐安装 Noto 字体；若禁用推荐依赖后出现方框字，请安装上述字体。独立二进制已嵌入五套文案，但仍需发行版的 Zenity、GTK 翻译、locale 与字体。

## Scope and contributions / 范围与校对

Translated content covers the application menu, preparation/restore warnings, common state errors, compatibility-check labels and summaries, progress stages, download summaries and command help. Raw external-command diagnostics, hardware descriptions, upstream polkit/file-chooser strings and the restored OS's first-user console follow their source/system language. Technical JSON fields remain stable for scripts.

已翻译应用菜单、准备及清空警告、常见状态错误、兼容性检查、阶段进度、下载摘要及命令帮助。外部程序原始报错、硬件描述、系统授权/文件选择器及恢复后系统首次开户控制台遵循其来源或系统语言，未宣称整个操作系统已翻译。

Catalogs live in `locale/*.json`; no runtime network translation is used. Keep identical keys and named placeholders, preserve deletion meaning, then run `node tests/catalogs.mjs`. Cloud tests check all five catalogs and confirmation paths, and render actual GTK 3/4 interfaces. Hindi, Spanish and Arabic wording would benefit from native-speaker review; reports and corrections are welcome through GitHub Issues.

文案位于 `locale/*.json`，无在线翻译请求。请保持键名及占位符一致，并运行校验脚本；欢迎母语使用者在 Issues 提交措辞改进。
