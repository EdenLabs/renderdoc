/******************************************************************************
 * The MIT License (MIT)
 *
 * Copyright (c) 2017-2026 Baldur Karlsson
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 ******************************************************************************/

#pragma once

// Application-level language constants for branching.
// These must not be passed to Scintilla directly.
// Values start at 2000 to avoid collision with Lexilla's
// SCLEX_AUTOMATIC (1000).
#define SCLEX_GLSL 2000
#define SCLEX_HLSL 2001
#define SCLEX_BUFFER 2002

#define SCINTILLA_COLOUR(r, g, b) (long(r) | (long(g) << 8) | (long(b) << 16))

class ScintillaEdit;

void ConfigureSyntax(ScintillaEdit *scintilla, int language);
