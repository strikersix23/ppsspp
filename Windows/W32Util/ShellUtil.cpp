#pragma warning(disable:4091)  // workaround bug in VS2015 headers

#include "Windows/stdafx.h"

#include <functional>
#include <thread>

#include "Common/Data/Encoding/Utf8.h"
#include "Common/File/FileUtil.h"
#include "Common/Data/Format/PNGLoad.h"
#include "ShellUtil.h"

#include <shobjidl.h>  // For IFileDialog and related interfaces
#include <shellapi.h>
#include <shlobj.h>
#include <commdlg.h>
#include <cderr.h>
#include <wrl/client.h>

namespace W32Util {
using Microsoft::WRL::ComPtr;

bool MoveToTrash(const Path &path) {
	ComPtr<IFileOperation> pFileOp;

	HRESULT hr = CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&pFileOp));
	if (FAILED(hr)) {
		return false;
	}

	// Set operation flags
	hr = pFileOp->SetOperationFlags(FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT);
	if (FAILED(hr)) {
		return false;
	}

	// Create a shell item from the file path
	ComPtr<IShellItem> pItem;
	hr = SHCreateItemFromParsingName(path.ToWString().c_str(), nullptr, IID_PPV_ARGS(&pItem));
	if (SUCCEEDED(hr)) {
		// Schedule the delete (move to recycle bin)
		hr = pFileOp->DeleteItem(pItem.Get(), nullptr);
		if (SUCCEEDED(hr)) {
			hr = pFileOp->PerformOperations(); // Execute
		}
	}

	if (SUCCEEDED(hr)) {
		INFO_LOG(Log::IO, "Moved file to trash successfully: %s", path.c_str());
		return true;
	} else {
		WARN_LOG(Log::IO, "Failed to move file to trash: %s", path.c_str());
		return false;
	}
}

std::string BrowseForFolder2(HWND parent, std::string_view title, std::string_view initialPath) {
	const std::wstring wtitle = ConvertUTF8ToWString(title);
	const std::wstring initialDir = ConvertUTF8ToWString(initialPath);

	std::wstring selectedFolder;

	// Create the FileOpenDialog object
	ComPtr<IFileDialog> pFileDialog;
	HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFileDialog));
	if (!SUCCEEDED(hr)) {
		return "";
	}
	// Set the options to select folders instead of files
	DWORD dwOptions;
	hr = pFileDialog->GetOptions(&dwOptions);
	if (SUCCEEDED(hr)) {
		hr = pFileDialog->SetOptions(dwOptions | FOS_PICKFOLDERS);
	} else {
		return "";
	}

	// Set the initial directory
	if (!initialDir.empty()) {
		ComPtr<IShellItem> pShellItem;
		hr = SHCreateItemFromParsingName(initialDir.c_str(), nullptr, IID_PPV_ARGS(&pShellItem));
		if (SUCCEEDED(hr)) {
			hr = pFileDialog->SetFolder(pShellItem.Get());
		}
	}
	pFileDialog->SetTitle(wtitle.c_str());

	// Show the dialog
	hr = pFileDialog->Show(parent);
	if (SUCCEEDED(hr)) {
		// Get the selected folder
		ComPtr<IShellItem> pShellItem;
		hr = pFileDialog->GetResult(&pShellItem);
		if (SUCCEEDED(hr)) {
			PWSTR pszFilePath = nullptr;
			hr = pShellItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
			if (SUCCEEDED(hr)) {
				selectedFolder = pszFilePath;
				CoTaskMemFree(pszFilePath);
			}
		}
	}

	return ConvertWStringToUTF8(selectedFolder);
}

	bool BrowseForFileName(bool _bLoad, HWND _hParent, const wchar_t *_pTitle,
		const wchar_t *_pInitialFolder, const wchar_t *_pFilter, const wchar_t *_pExtension,
		std::string &_strFileName) {
		// Let's hope this is large enough, don't want to trigger the dialog twice...
		std::wstring filenameBuffer(32768 * 10, '\0');

		OPENFILENAME ofn{ sizeof(OPENFILENAME) };

		auto resetFileBuffer = [&] {
			ofn.nMaxFile = (DWORD)filenameBuffer.size();
			ofn.lpstrFile = &filenameBuffer[0];
			if (!_strFileName.empty())
				wcsncpy(ofn.lpstrFile, ConvertUTF8ToWString(_strFileName).c_str(), filenameBuffer.size() - 1);
		};

		resetFileBuffer();
		ofn.lpstrInitialDir = _pInitialFolder;
		ofn.lpstrFilter = _pFilter;
		ofn.lpstrFileTitle = nullptr;
		ofn.nMaxFileTitle = 0;
		ofn.lpstrDefExt = _pExtension;
		ofn.hwndOwner = _hParent;
		ofn.Flags = OFN_NOCHANGEDIR | OFN_EXPLORER;
		if (!_bLoad)
			ofn.Flags |= OFN_HIDEREADONLY;

		int success = _bLoad ? GetOpenFileName(&ofn) : GetSaveFileName(&ofn);
		if (success == 0 && CommDlgExtendedError() == FNERR_BUFFERTOOSMALL) {
			size_t sz = *(unsigned short *)&filenameBuffer[0];
			// Documentation is unclear if this is WCHARs to CHARs.
			filenameBuffer.resize(filenameBuffer.size() + sz * 2);
			resetFileBuffer();
			success = _bLoad ? GetOpenFileName(&ofn) : GetSaveFileName(&ofn);
		}

		if (success) {
			_strFileName = ConvertWStringToUTF8(ofn.lpstrFile);
			return true;
		}
		return false;
	}
	
	std::vector<std::string> BrowseForFileNameMultiSelect(bool _bLoad, HWND _hParent, const wchar_t *_pTitle,
		const wchar_t *_pInitialFolder, const wchar_t *_pFilter, const wchar_t *_pExtension) {
		// Let's hope this is large enough, don't want to trigger the dialog twice...
		std::wstring filenameBuffer(32768 * 10, '\0');

		OPENFILENAME ofn{ sizeof(OPENFILENAME) };

		auto resetFileBuffer = [&] {
			ofn.nMaxFile = (DWORD)filenameBuffer.size();
			ofn.lpstrFile = &filenameBuffer[0];
		};

		resetFileBuffer();
		ofn.lpstrInitialDir = _pInitialFolder;
		ofn.lpstrFilter = _pFilter;
		ofn.lpstrFileTitle = nullptr;
		ofn.nMaxFileTitle = 0;
		ofn.lpstrDefExt = _pExtension;
		ofn.hwndOwner = _hParent;
		ofn.Flags = OFN_NOCHANGEDIR | OFN_EXPLORER | OFN_ALLOWMULTISELECT;
		if (!_bLoad)
			ofn.Flags |= OFN_HIDEREADONLY;

		std::vector<std::string> files;
		int success = _bLoad ? GetOpenFileName(&ofn) : GetSaveFileName(&ofn);
		if (success == 0 && CommDlgExtendedError() == FNERR_BUFFERTOOSMALL) {
			size_t sz = *(unsigned short *)&filenameBuffer[0];
			// Documentation is unclear if this is WCHARs to CHARs.
			filenameBuffer.resize(filenameBuffer.size() + sz * 2);
			resetFileBuffer();
			success = _bLoad ? GetOpenFileName(&ofn) : GetSaveFileName(&ofn);
		}

		if (success) {
			std::string directory = ConvertWStringToUTF8(ofn.lpstrFile);
			wchar_t *temp = ofn.lpstrFile;
			temp += wcslen(temp) + 1;
			if (*temp == 0) {
				//we only got one file
				files.push_back(directory);
			} else {
				while (*temp) {
					files.emplace_back(directory + "\\" + ConvertWStringToUTF8(temp));
					temp += wcslen(temp) + 1;
				}
			}
		}
		return files;
	}

	std::string UserDocumentsPath() {
		std::string result;
		HMODULE shell32 = GetModuleHandle(L"shell32.dll");
		typedef HRESULT(WINAPI *SHGetKnownFolderPath_f)(REFKNOWNFOLDERID rfid, DWORD dwFlags, HANDLE hToken, PWSTR *ppszPath);
		SHGetKnownFolderPath_f SHGetKnownFolderPath_ = nullptr;
		if (shell32)
			SHGetKnownFolderPath_ = (SHGetKnownFolderPath_f)GetProcAddress(shell32, "SHGetKnownFolderPath");
		if (SHGetKnownFolderPath_) {
			PWSTR path = nullptr;
			if (SHGetKnownFolderPath_(FOLDERID_Documents, 0, nullptr, &path) == S_OK) {
				result = ConvertWStringToUTF8(path);
			}
			if (path)
				CoTaskMemFree(path);
		} else {
			wchar_t path[MAX_PATH];
			if (SHGetFolderPath(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, path) == S_OK) {
				result = ConvertWStringToUTF8(path);
			}
		}

		return result;
	}


// http://msdn.microsoft.com/en-us/library/aa969393.aspx
static HRESULT CreateLink(LPCWSTR lpszPathObj, LPCWSTR lpszArguments, LPCWSTR lpszPathLink, LPCWSTR lpszDesc, LPCWSTR lpszIcon, int iconIndex) {
	HRESULT hres;
	ComPtr<IShellLink> psl;
	hres = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	if (FAILED(hres))
		return hres;

	// Get a pointer to the IShellLink interface. It is assumed that CoInitialize
	// has already been called.
	hres = CoCreateInstance(__uuidof(ShellLink), NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&psl));
	if (SUCCEEDED(hres) && psl) {
		ComPtr<IPersistFile> ppf;

		// Set the path to the shortcut target and add the description. 
		psl->SetPath(lpszPathObj);
		psl->SetArguments(lpszArguments);
		psl->SetDescription(lpszDesc);
		if (lpszIcon) {
			psl->SetIconLocation(lpszIcon, iconIndex);
		}

		// Query IShellLink for the IPersistFile interface, used for saving the 
		// shortcut in persistent storage. 
		hres = psl.As(&ppf);

		if (SUCCEEDED(hres) && ppf) {
			// Save the link by calling IPersistFile::Save. 
			hres = ppf->Save(lpszPathLink, TRUE);
		}
	}
	CoUninitialize();

	return hres;
}

bool CreateDesktopShortcut(std::string_view argumentPath, std::string_view gameTitleStr, const Path &icoFile) {
	// Get the desktop folder
	wchar_t *pathbuf = new wchar_t[4096];
	SHGetFolderPath(0, CSIDL_DESKTOPDIRECTORY, NULL, SHGFP_TYPE_CURRENT, pathbuf);

	std::string gameTitle(gameTitleStr);
	// Sanitize the game title for banned characters.
	const char bannedChars[] = "<>:\"/\\|?*";
	for (size_t i = 0; i < gameTitle.size(); i++) {
		for (char c : bannedChars) {
			if (gameTitle[i] == c) {
				gameTitle[i] = '_';
				break;
			}
		}
	}

	wcscat(pathbuf, L"\\");
	wcscat(pathbuf, ConvertUTF8ToWString(gameTitle).c_str());
	wcscat(pathbuf, L".lnk");

	std::wstring moduleFilename;
	size_t sz;
	do {
		moduleFilename.resize(moduleFilename.size() + MAX_PATH);
		// On failure, this will return the same value as passed in, but success will always be one lower.
		sz = GetModuleFileName(nullptr, &moduleFilename[0], (DWORD)moduleFilename.size());
	} while (sz >= moduleFilename.size());
	moduleFilename.resize(sz);

	// Need to flip the slashes in the filename.

	std::string sanitizedArgument(argumentPath);
	for (size_t i = 0; i < sanitizedArgument.size(); i++) {
		if (sanitizedArgument[i] == '/') {
			sanitizedArgument[i] = '\\';
		}
	}

	sanitizedArgument = "\"" + sanitizedArgument + "\"";

	std::wstring icon;
	if (!icoFile.empty()) {
		icon = icoFile.ToWString();
	}

	CreateLink(moduleFilename.c_str(), ConvertUTF8ToWString(sanitizedArgument).c_str(), pathbuf, ConvertUTF8ToWString(gameTitle).c_str(), icon.empty() ? nullptr : icon.c_str(), 0);

	// TODO: Also extract the game icon and convert to .ico, put it somewhere under Memstick, and set it.

	delete[] pathbuf;
	return false;
}

// Function to create an icon file from PNG image data (these icons require Windows Vista).
// The Old New Thing comes to the rescue again! ChatGPT failed miserably.
// https://devblogs.microsoft.com/oldnewthing/20101018-00/?p=12513
// https://devblogs.microsoft.com/oldnewthing/20101022-00/?p=12473
bool CreateICOFromPNGData(const uint8_t *imageData, size_t imageDataSize, const Path &icoPath) {
	if (imageDataSize <= sizeof(PNGHeaderPeek)) {
		return false;
	}
	// Parse the PNG
	PNGHeaderPeek pngHeader;
	memcpy(&pngHeader, imageData, sizeof(PNGHeaderPeek));
	if (pngHeader.Width() > 256 || pngHeader.Height() > 256) {
		// Reject the png as an icon.
		return false;
	}

	struct IconHeader {
		uint16_t reservedZero;
		uint16_t type;  // should be 1
		uint16_t imageCount;
	};
	IconHeader hdr{ 0, 1, 1 };
	struct IconDirectoryEntry {
		BYTE  bWidth;
		BYTE  bHeight;
		BYTE  bColorCount;
		BYTE  bReserved;
		WORD  wPlanes;
		WORD  wBitCount;
		DWORD dwBytesInRes;
		DWORD dwImageOffset;
	};
	IconDirectoryEntry entry{};
	entry.bWidth = pngHeader.Width();
	entry.bHeight = pngHeader.Height();
	entry.bColorCount = 0;
	entry.dwBytesInRes = (DWORD)imageDataSize;
	entry.wPlanes = 1;
	entry.wBitCount = 32;
	entry.dwImageOffset = sizeof(hdr) + sizeof(entry);

	FILE *file = File::OpenCFile(icoPath, "wb");
	if (!file) {
		return false;
	}
	fwrite(&hdr, sizeof(hdr), 1, file);
	fwrite(&entry, sizeof(entry), 1, file);
	fwrite(imageData, 1, imageDataSize, file);
	fclose(file);
	return true;
}

}  // namespace W32Util
