# BetterMultiSelection

[![Build status](https://ci.appveyor.com/api/projects/status/github/dail8859/BetterMultiSelection?branch=master&svg=true)](https://ci.appveyor.com/project/dail8859/BetterMultiSelection/branch/master)

Notepad++ plugin to provide better cursor movements when using multiple selections.

**Note:** This is still in early development. It has not been tested with non-US keyboard layouts. This does have some minor quirks (such as the cursor not always blinking correctly) that are a side effect of the implementation.

![Demo](/img/Demo.gif)

## Usage
Make multiple or rectangular selections and move the cursor. Supported cursor movements are:

- Left
- Right
- Home
- End
- Word Left (`Ctrl+Left`)
- Word Right (`Ctrl+Right`)

You can also hold down `Shift` to extend the selections, or press `Enter` to insert new lines.

## Installation
Install the plugin by downloading it from the [Release](https://github.com/dail8859/BetterMultiSelection/releases) page and copy `BetterMultiSelection.dll` to your `plugins` folder.

## Development
The code has been developed using MSVC 2015. Building the code will generate the DLL which can be used by Notepad++. For convenience, MSVC copies the DLL into the Notepad++ plugin directory.

## License
This code is released under the [GNU General Public License version 2](http://www.gnu.org/licenses/gpl-2.0.txt).
