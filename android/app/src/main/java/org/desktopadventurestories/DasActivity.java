// SPDX-License-Identifier: GPL-3.0-or-later
package org.desktopadventurestories;

import org.libsdl.app.SDLActivity;

/**
 * Android entry point. SDLActivity starts the native game in libmain.so,
 * which is built by CMake from src/app.
 */
public class DasActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL3", "main"};
    }
}
