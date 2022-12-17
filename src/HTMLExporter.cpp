#include "stdafx.h"
#include "Scintilla.h"
#include "ExportStructs.h"
#include "Exporter.h"
#include "HTMLExporter.h"
#include <stdio.h>

HTMLExporter::HTMLExporter(void) {
	setClipboardID(RegisterClipboardFormat(CF_HTML));
	if (getClipboardID() == 0) {
		MessageBox(NULL, TEXT("Unable to register clipboard format HTML!"), TEXT("Error"), MB_OK);
	}
}

HTMLExporter::~HTMLExporter(void) {
}

bool HTMLExporter::exportData(ExportData * ed) {

	//estimate buffer size needed
	char * buffer = ed->csd->dataBuffer;
	size_t totalBytesNeeded = 1;	//zero terminator
	bool addHeader = ed->isClipboard;	//true if putting data on clipboard
	bool isUTF8 = ed->csd->currentCodePage == SC_CP_UTF8 ? true : false;
	
	//totalBytesNeeded += EXPORT_SIZE_HTML_STATIC +EXPORT_SIZE_HTML_STYLE * (ed->csd->nrUsedStyles - 1) + ed->csd->totalFontStringLength + EXPORT_SIZE_HTML_SWITCH * ed->csd->nrStyleSwitches;
	totalBytesNeeded += static_cast<size_t>(EXPORT_SIZE_HTML_STATIC) + static_cast<size_t>(ed->csd->totalFontStringLength) * 2 + static_cast<size_t>(79) * ed->csd->nrStyleSwitches;
	if (addHeader)
		totalBytesNeeded += EXPORT_SIZE_HTML_CLIPBOARD;
	if (isUTF8)
		totalBytesNeeded += EXPORT_SIZE_HTML_UTF8;
	int startHTML = 105, endHTML = 0, startFragment = 0, endFragment = 0;

	unsigned char testChar = 0;
	for(int i = 0; i < ed->csd->nrChars; i++) {
		testChar = buffer[(i*2)];
		switch(testChar) {
			case '\r':
				if (buffer[(i*2)+2] == '\n')
					break;
			case '\n':
				totalBytesNeeded += 4;	//	'<br>'
				break;
			case '<':
				totalBytesNeeded += 4;	// '&lt;'
				break;
			case '>':
				totalBytesNeeded += 4;	// '&gt;'
				break;
			case '&':
				totalBytesNeeded += 5;	// '&amp;'
				break;
			case ' ':
				totalBytesNeeded += 6;	// '&nbsp;'
				break;
			case '\t':
				totalBytesNeeded += static_cast<size_t>(ed->csd->tabSize) * 6;
				break;
			default:
				if (testChar < 0x20)	//	ignore control characters
					break;
				totalBytesNeeded += 1; //	'char'
				break;
		}
	}

	int currentBufferOffset = 0;

	char * clipbuffer = new char[totalBytesNeeded];
	clipbuffer[0] = 0;

	//add CF_HTML header if needed, return later to fill in the blanks
	if (addHeader) {
		currentBufferOffset += sprintf(clipbuffer+currentBufferOffset, "Version:0.9\r\nStartHTML:0000000105\r\nEndHTML:0000000201\r\nStartFragment:0000000156\r\nEndFragment:0000000165\r\n");
	}
	//end CF_HTML header

	//begin building context
	//proper doctype to pass validation, just because it looks good
	currentBufferOffset += sprintf(clipbuffer+currentBufferOffset,
			"<!DOCTYPE html PUBLIC \"-//W3C//DTD HTML 4.01//EN\" \"http://www.w3.org/TR/1999/REC-html401-19991224/strict.dtd\">\r\n"
		);
	currentBufferOffset += sprintf(clipbuffer+currentBufferOffset, "<html>\r\n");
	currentBufferOffset += sprintf(clipbuffer+currentBufferOffset, "<head>\r\n");

	currentBufferOffset += sprintf(clipbuffer+currentBufferOffset, "<META http-equiv=Content-Type content=\"text/html; charset=");
	if (isUTF8) {
		currentBufferOffset += sprintf(clipbuffer+currentBufferOffset, "UTF-8");
	} else {
		currentBufferOffset += sprintf(clipbuffer+currentBufferOffset, "windows-1252");
	}
	currentBufferOffset += sprintf(clipbuffer+currentBufferOffset, "\">\r\n");
	currentBufferOffset += sprintf(clipbuffer+currentBufferOffset, "<title>Exported from Notepad++</title>\r\n");

	StyleData * currentStyle, * defaultStyle;
	defaultStyle = (ed->csd->styles) + STYLE_DEFAULT;

	unsigned int arrStyles[NRSTYLES]{};

	for(int i = 0; i < NRSTYLES; i++) {
		if (i == STYLE_DEFAULT)
			continue;
		arrStyles[i] = 0;
		currentStyle = (ed->csd->styles)+i;
		if (ed->csd->usedStyles[i] == true) {

			if (currentStyle->underlined)
				arrStyles[i] = arrStyles[i] | 1;
			if (currentStyle->italic != defaultStyle->italic) {
				if (currentStyle->italic)
					arrStyles[i] = arrStyles[i] | 2;
			}
			if (currentStyle->bold != defaultStyle->bold) {
				if (currentStyle->bold)
					arrStyles[i] = arrStyles[i] | 4;
			}
			if (currentStyle->fgColor != defaultStyle->fgColor) {
				arrStyles[i] = arrStyles[i] | 8;
				arrStyles[i] = arrStyles[i] | (currentStyle->fgColor<<8);
			}

		}
	}

	currentBufferOffset += sprintf(clipbuffer+currentBufferOffset, "</head>\r\n");
	currentBufferOffset += sprintf(clipbuffer+currentBufferOffset, "<body>\r\n");

	//end building context

	//add StartFragment if doing CF_HTML
	if (addHeader) {
		currentBufferOffset += sprintf(clipbuffer+currentBufferOffset, "<!--StartFragment-->\r\n");
	}
	startFragment = currentBufferOffset;
	//end StartFragment

	currentBufferOffset += sprintf(clipbuffer+currentBufferOffset, "<span style=\"");
	currentBufferOffset += sprintf(clipbuffer + currentBufferOffset,
			"font-family: &quot;%s&quot;"
			"font-size: 12px;"
			"line-height: 1;",
			defaultStyle->fontString
		);
	currentBufferOffset += sprintf(clipbuffer+currentBufferOffset, "\">");
	currentBufferOffset += sprintf(clipbuffer+currentBufferOffset, "<font face=\"%s\" size=\"2\">", defaultStyle->fontString);

//-------Dump text to HTML
	char * tabBuffer = new char[static_cast<size_t>(ed->csd->tabSize)*6 + 1];
	tabBuffer[0] = 0;
	for(int i = 0; i < ed->csd->tabSize; i++) {
		//strcat(tabBuffer, "&nbsp;");
		sprintf(tabBuffer+i*6, "&#160;");
	}

	int nrCharsSinceLinebreak = -1, nrTabCharsToSkip = 0;
	int lastStyle = -1;
	unsigned char currentChar;
	bool openFont = false;
	bool openBold = false;
	bool openItalic = false;
	bool openUnderline = false;

	for(int i = 0; i < ed->csd->nrChars; i++) {
		//print new span object if style changes
		if (buffer[i*2+1] != lastStyle) {
			if (openUnderline) {
				currentBufferOffset += sprintf(clipbuffer + currentBufferOffset, "</u>");
				openUnderline = false;
			}
			if (openItalic) {
				currentBufferOffset += sprintf(clipbuffer + currentBufferOffset, "</i>");
				openItalic = false;
			}
			if (openBold) {
				currentBufferOffset += sprintf(clipbuffer + currentBufferOffset, "</b>");
				openBold = false;
			}
			if (openFont) {
				currentBufferOffset += sprintf(clipbuffer + currentBufferOffset, "</font></span>");
				openFont = false;
			}
			
			lastStyle = buffer[i*2+1];
			if ((arrStyles[lastStyle] & 8) == 8 && buffer[(i * 2)] != '\n' && buffer[(i * 2)] != '\r' && buffer[(i * 2)] != ' ') {
				unsigned int htmlRed   = (arrStyles[lastStyle] >>  8) & 0xFF;
				unsigned int htmlGreen = (arrStyles[lastStyle] >> 16) & 0xFF;
				unsigned int htmlBlue  = (arrStyles[lastStyle] >> 24) & 0xFF;
				currentBufferOffset += sprintf(clipbuffer + currentBufferOffset, "<span style=\"color: #%02X%02X%02X;\">", htmlRed, htmlGreen, htmlBlue);
				currentBufferOffset += sprintf(clipbuffer + currentBufferOffset, "<font color=\"#%02X%02X%02X\">", htmlRed, htmlGreen, htmlBlue);
				openFont = true;
			}
			if ((arrStyles[lastStyle] & 4) == 4) {
				currentBufferOffset += sprintf(clipbuffer + currentBufferOffset, "<b>");
				openBold = true;
			}
			if ((arrStyles[lastStyle] & 2) == 2) {
				currentBufferOffset += sprintf(clipbuffer + currentBufferOffset, "<i>");
				openItalic = true;
			}
			if ((arrStyles[lastStyle] & 1) == 1) {
				currentBufferOffset += sprintf(clipbuffer + currentBufferOffset, "<u>");
				openUnderline = true;
			}

		}

		//print character, parse special ones
		currentChar = buffer[(i*2)];
		nrCharsSinceLinebreak++;
		switch(currentChar) {
			case '\r':
				if (buffer[(i*2)+2] == '\n')
					break;
			case '\n':
				currentBufferOffset += sprintf(clipbuffer + currentBufferOffset, "<br>");
				nrCharsSinceLinebreak = -1;
				break;
			case '<':
				currentBufferOffset += sprintf(clipbuffer + currentBufferOffset, "&lt;");
				break;
			case '>':
				currentBufferOffset += sprintf(clipbuffer + currentBufferOffset, "&gt;");
				break;
			case '&':
				currentBufferOffset += sprintf(clipbuffer + currentBufferOffset, "&amp;");
				break;
			case ' ':
				currentBufferOffset += sprintf(clipbuffer + currentBufferOffset, "&#160;");
				break;
			case '\t':
				nrTabCharsToSkip = nrCharsSinceLinebreak%(ed->csd->tabSize);
				currentBufferOffset += sprintf(clipbuffer+currentBufferOffset, "%s", tabBuffer + (nrTabCharsToSkip * 6));
				nrCharsSinceLinebreak += ed->csd->tabSize - nrTabCharsToSkip - 1;
				break;
			default:
				if (currentChar < 0x20)	//ignore control characters
					break;
				currentBufferOffset += sprintf(clipbuffer+currentBufferOffset, "%c", currentChar);
				break;
		}
	}

	if (openUnderline) {
		currentBufferOffset += sprintf(clipbuffer + currentBufferOffset, "</u>");
	}
	if (openItalic) {
		currentBufferOffset += sprintf(clipbuffer + currentBufferOffset, "</i>");
	}
	if (openBold) {
		currentBufferOffset += sprintf(clipbuffer + currentBufferOffset, "</b>");
	}
	if (openFont) {
		currentBufferOffset += sprintf(clipbuffer+currentBufferOffset, "</font></span>");
	}

	currentBufferOffset += sprintf(clipbuffer+currentBufferOffset, "</font></span>");

	delete [] tabBuffer;

	//add EndFragment if doing CF_HTML
	endFragment = currentBufferOffset;
	if (addHeader) {
		currentBufferOffset += sprintf(clipbuffer+currentBufferOffset, "<!--EndFragment-->\r\n");
	}
	//end EndFragment

	//add closing context
	currentBufferOffset += sprintf(clipbuffer+currentBufferOffset, "</body>\r\n</html>");
	endHTML = currentBufferOffset;
	currentBufferOffset += sprintf(clipbuffer + currentBufferOffset, "%c", 0);


	//if doing CF_HTML, fill in header data
	if (addHeader) {
		char number[11];
		sprintf(number, "%.10d", startHTML);
		memcpy(clipbuffer + 23, number, 10);
		sprintf(number, "%.10d", endHTML);
		memcpy(clipbuffer + 43, number, 10);
		sprintf(number, "%.10d", startFragment);
		memcpy(clipbuffer + 69, number, 10);
		sprintf(number, "%.10d", endFragment);
		memcpy(clipbuffer + 93, number, 10);
	}
	//end header

	HGLOBAL hHTMLBuffer = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, static_cast<size_t>(currentBufferOffset));
	if (hHTMLBuffer == nullptr) {
		return false;
	}
	
	char* clipbufferA = (char*)GlobalLock(hHTMLBuffer);
	if (clipbufferA == nullptr) {
		GlobalFree(hHTMLBuffer);
		return false;
	}

	memcpy(clipbufferA, clipbuffer, currentBufferOffset);
	delete[] clipbuffer;

	GlobalUnlock(hHTMLBuffer);
	ed->hBuffer = hHTMLBuffer;
	ed->bufferSize = currentBufferOffset;
	return true;
}

TCHAR * HTMLExporter::getClipboardType() {
	return CF_HTML;
}
