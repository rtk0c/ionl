# The `TextEdit` widget

## `TextBuffer`
The text buffer is a gap buffer. It consists of an array of Unicode codepoints, split into 3 sections: the front,
the gap, and the back. The front and the back represent actual text data, while the gap sits in the middle as a region
of unused space for writing.

The buffer consists of lines divided by `\n` codepoints (note that this means the last "paragraph" in the buffer doesn't
end with \n). Lone `\r` and `\r\n` sequences should be replaced by `\n` when text is imported to the buffer. The buffer
is not null terminated.

## Cursor handling
> **NOTE:** a cursor is also known as a "caret" in other text editors.
> 
> **NOTE:** this cursor is completely orthogonal to `ImGui::GetCursorPos()`.

## Rendering
For optimization, `TextEdit` does not directly participate in ImGui's main `ImDrawList`-based rendering loop. Instead,
it draws to its own copy of a vertex and index buffer, and directly submits those to the GPU through {TODO direct
rendering API addition} on each frame.