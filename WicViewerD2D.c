#define UNICODE
#define COBJMACROS
#define IID_PPV_ARGS(ppType) ((void**)(ppType))

#include <windows.h>
#include <initguid.h>
#include <d2d1.h>
#include <wincodec.h>
#include "WICViewerD2D.h"

typedef struct {
	HINSTANCE               m_hInst;
    IWICImagingFactory     *m_pIWICFactory;
    ID2D1Factory           *m_pD2DFactory;
    ID2D1HwndRenderTarget  *m_pRT;
    ID2D1Bitmap            *m_pD2DBitmap;
    IWICFormatConverter    *m_pConvertedSourceBitmap;
} Factory;

Factory* GetFactoryPtr(HWND hWnd)
{
    LONG_PTR ptr = GetWindowLongPtr(hWnd, GWLP_USERDATA);
    Factory *factory = (Factory *)(ptr);
    return factory;
}

BOOL LocateImageFile(HWND hWnd, LPTSTR pszFileName, DWORD cchFileName)
{
    pszFileName[0] = L'\0';

    OPENFILENAME ofn;
    ZeroMemory(&ofn, sizeof(ofn));

    ofn.lStructSize     = sizeof(ofn);
    ofn.hwndOwner       = hWnd;
    ofn.lpstrFilter     = L"All Image Files\0"              L"*.bmp;*.dib;*.wdp;*.mdp;*.hdp;*.gif;*.png;*.jpg;*.jpeg;*.tif;*.ico\0"
                          L"Windows Bitmap\0"               L"*.bmp;*.dib\0"
                          L"High Definition Photo\0"        L"*.wdp;*.mdp;*.hdp\0"
                          L"Graphics Interchange Format\0"  L"*.gif\0"
                          L"Portable Network Graphics\0"    L"*.png\0"
                          L"JPEG File Interchange Format\0" L"*.jpg;*.jpeg\0"
                          L"Tiff File\0"                    L"*.tif\0"
                          L"Icon\0"                         L"*.ico\0"
                          L"All Files\0"                    L"*.*\0"
                          L"\0";
    ofn.lpstrFile       = pszFileName;
    ofn.nMaxFile        = cchFileName;
    ofn.lpstrTitle      = L"Open Image";
    ofn.Flags           = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    // Display the Open dialog box. 
    return (GetOpenFileName(&ofn) == TRUE);
}

HRESULT CreateDeviceResources(HWND hWnd)
{
    HRESULT hr = S_OK;

	Factory *factory = GetFactoryPtr(hWnd);

    if (!factory->m_pRT)
    {
        RECT rc;
        hr = GetClientRect(hWnd, &rc) ? S_OK : E_FAIL;

        if (SUCCEEDED(hr))
        {
            D2D1_PIXEL_FORMAT PixelFormat = { DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_UNKNOWN };
            D2D1_RENDER_TARGET_PROPERTIES renderTargetProperties = {
				D2D1_RENDER_TARGET_TYPE_DEFAULT,
				PixelFormat,
				0.0,
				0.0,
				D2D1_RENDER_TARGET_USAGE_NONE,
				D2D1_FEATURE_LEVEL_DEFAULT
				};

            // Set the DPI to be the default system DPI to allow direct mapping
            // between image pixels and desktop pixels in different system DPI settings
            renderTargetProperties.dpiX = DEFAULT_DPI;
            renderTargetProperties.dpiY = DEFAULT_DPI;

            D2D1_SIZE_U size = { rc.right - rc.left, rc.bottom - rc.top };
			D2D1_HWND_RENDER_TARGET_PROPERTIES hwndRenderTargetProperties = { hWnd, size, D2D1_PRESENT_OPTIONS_NONE };
            hr = ID2D1Factory_CreateHwndRenderTarget(
				factory->m_pD2DFactory,
				&renderTargetProperties,
				&hwndRenderTargetProperties,
				&factory->m_pRT
				);
        }
    }
    return hr;
}

HRESULT CreateD2DBitmapFromFile(HWND hWnd)
{
    HRESULT hr = S_OK;

    TCHAR szFileName[MAX_PATH];

    // Step 1: Create the open dialog box and locate the image file
    if (LocateImageFile(hWnd, szFileName, ARRAYSIZE(szFileName)))
    {
        // Step 2: Decode the source image

		Factory *factory = GetFactoryPtr(hWnd);
		
        // Create a decoder
        IWICBitmapDecoder *pDecoder = NULL;

        hr = IWICImagingFactory_CreateDecoderFromFilename(
			factory->m_pIWICFactory,
			szFileName,
			NULL,
			GENERIC_READ,
			WICDecodeMetadataCacheOnDemand,
			&pDecoder
			);

        // Retrieve the first frame of the image from the decoder
        IWICBitmapFrameDecode *pFrame = NULL;

        if (SUCCEEDED(hr))
        {
            hr = IWICBitmapDecoder_GetFrame(pDecoder,0,&pFrame);
        }
		
        //Step 3: Format convert the frame to 32bppPBGRA
        if (SUCCEEDED(hr))
        {
            if (factory->m_pConvertedSourceBitmap)
			{
            	IWICFormatConverter_Release(factory->m_pConvertedSourceBitmap);
            	factory->m_pConvertedSourceBitmap = NULL;
			}
            hr = IWICImagingFactory_CreateFormatConverter(factory->m_pIWICFactory,&(factory)->m_pConvertedSourceBitmap);
        }

        if (SUCCEEDED(hr))
        {
            hr = IWICFormatConverter_Initialize(
            	factory->m_pConvertedSourceBitmap,
            	(IWICBitmapSource *)pFrame,
            	&GUID_WICPixelFormat32bppPBGRA,
            	WICBitmapDitherTypeNone,
            	NULL,
            	0.f,
            	WICBitmapPaletteTypeCustom
				);
        }

        //Step 4: Create render target and D2D bitmap from IWICBitmapSource
        if (SUCCEEDED(hr))
        {
            hr = CreateDeviceResources(hWnd);
        }

        if (SUCCEEDED(hr))
        {
            // Need to release the previous D2DBitmap if there is one
            if (factory->m_pD2DBitmap)
			{
				ID2D1Bitmap_Release(factory->m_pD2DBitmap);
            	factory->m_pD2DBitmap = NULL;
			}
            
            hr = ID2D1DCRenderTarget_CreateBitmapFromWicBitmap(
				factory->m_pRT,
				(IWICBitmapSource *)factory->m_pConvertedSourceBitmap,
				NULL,
				&factory->m_pD2DBitmap
				);
        }
        IWICBitmapDecoder_Release(pDecoder);
        pDecoder = NULL;
        IWICBitmapFrameDecode_Release(pFrame);
        pFrame = NULL;
    }
    return hr;
}

LRESULT OnPaint(HWND hWnd)
{
    HRESULT hr = S_OK;
    PAINTSTRUCT ps;

	Factory *factory = GetFactoryPtr(hWnd);

    if (BeginPaint(hWnd, &ps))
    {
        // Create render target if not yet created
        hr = CreateDeviceResources(hWnd);
        
        D2D1_WINDOW_STATE wState = ID2D1HwndRenderTarget_CheckWindowState(factory->m_pRT);

        if (SUCCEEDED(hr) && !(wState & D2D1_WINDOW_STATE_OCCLUDED))
        {
            ID2D1HwndRenderTarget_BeginDraw(factory->m_pRT);

			D2D1_MATRIX_3X2_F identity = { 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f };
            ID2D1DCRenderTarget_SetTransform(factory->m_pRT, &identity);

            // Clear the background
            D2D1_COLOR_F white = { 1.0f, 1.0f, 1.0f, 1.0f };
            ID2D1HwndRenderTarget_Clear(factory->m_pRT, &white);

			// Fix for GetSize() returning wrong data
			intptr_t *real_rtSize;
			// Fix for GetSize() messing m_pRT Vtbl addr on x86_64
			const ID2D1HwndRenderTargetVtbl *m_pRTVtbl;
			m_pRTVtbl = factory->m_pRT->lpVtbl;
			
			
			/* temporary workaround for change in gcc 9.x */
		#if __GNUC__ < 9
			D2D1_SIZE_F rtSize = ID2D1HwndRenderTarget_GetSize(factory->m_pRT);
		#else
			D2D1_SIZE_F rtSize;
			asm("mov -0x10(%rbp),%rax");
			asm("mov 0x18(%rax),%rax");
   			asm("mov (%rax),%rax");
   			asm("mov 0x1a8(%rax),%rax");
   			asm("mov -0x10(%rbp),%rdx");
   			asm("mov 0x18(%rdx),%rdx");
   			asm("mov %rdx,%rcx");
   			asm("callq *%rax");
   			asm("mov %rax,-0xb0(%rbp)");
   		#endif
			
			// rtSize.width contain real rtSize addr - copy addr to *real_rtSize
			// then write the contents of real_rtSize back to rtSize
			memcpy(&real_rtSize,(intptr_t *)&rtSize.width,sizeof(intptr_t));
			memcpy(&rtSize, real_rtSize, sizeof(D2D1_SIZE_F));
			// GetSize() messes m_pRT addr on x86_64, so we restore it
			factory->m_pRT->lpVtbl = m_pRTVtbl;
			
			D2D1_RECT_F rectangle = { 0.0f, 0.0f, rtSize.width, rtSize.height };
			
            // D2DBitmap may have been released due to device loss. 
            // If so, re-create it from the source bitmap
            if (factory->m_pConvertedSourceBitmap && !factory->m_pD2DBitmap)
            {
                ID2D1DCRenderTarget_CreateBitmapFromWicBitmap(
					factory->m_pRT,
					(IWICBitmapSource *)factory->m_pConvertedSourceBitmap,
					NULL,
					&factory->m_pD2DBitmap
					);
            }
            
            // Draws an image and scales it to the current window size
            if (factory->m_pD2DBitmap)
            {
                ID2D1HwndRenderTarget_DrawBitmap(factory->m_pRT, factory->m_pD2DBitmap, &rectangle, 1.0, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, NULL);
            }

            hr = ID2D1HwndRenderTarget_EndDraw(factory->m_pRT, NULL, NULL);

            // In case of device loss, discard D2D render target and D2DBitmap
            // They will be re-created in the next rendering pass
            if (hr == (HRESULT)D2DERR_RECREATE_TARGET)
            {
                ID2D1Bitmap_Release(factory->m_pD2DBitmap);
                factory->m_pD2DBitmap = NULL;
                ID2D1HwndRenderTarget_Release(factory->m_pRT);
                factory->m_pRT = NULL;
                // Force a re-render
                hr = InvalidateRect(hWnd, NULL, TRUE)? S_OK : E_FAIL;
            }
        }

        EndPaint(hWnd, &ps);
    }

    return SUCCEEDED(hr) ? 0 : 1;
}  

LRESULT WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {

        case WM_COMMAND:
        {
            // Parse the menu selections:
            switch (LOWORD(wParam))
            {
                case IDM_FILE:
                {
                    if (SUCCEEDED(CreateD2DBitmapFromFile(hWnd)))
                    {
                        InvalidateRect(hWnd, NULL, TRUE);
                    }
                    else
                    {
                        MessageBox(hWnd, L"Failed to load image, select a new one.", L"Application Error", MB_ICONEXCLAMATION | MB_OK);
                    }
                    break;
                }
                case IDM_EXIT:
                {
                    PostMessage(hWnd, WM_CLOSE, 0, 0);
                    break;
                }
            }
            break;
        }
        case WM_SIZE:
        {
        	Factory *factory = GetFactoryPtr(hWnd);
        	
            D2D1_SIZE_U size = { LOWORD(lParam), HIWORD(lParam) };

            if (factory->m_pRT)
            {
                // If we couldn't resize, release the device and we'll recreate it
                // during the next render pass.
                if (FAILED(ID2D1HwndRenderTarget_Resize(factory->m_pRT, &size)))
                {
                    ID2D1HwndRenderTarget_Release(factory->m_pRT);
                    factory->m_pRT = NULL;
                    ID2D1Bitmap_Release(factory->m_pD2DBitmap);
                    factory->m_pD2DBitmap = NULL;
                }
            }
            break;
        }
        case WM_PAINT:
        {
            return OnPaint(hWnd);
        }
        case WM_DESTROY:
        {
            PostQuitMessage(0);
            return 0;
        }
        default:
            return DefWindowProc(hWnd, uMsg, wParam, lParam);
    }

    return 0;
}

static LRESULT CALLBACK s_WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    Factory *fa;
    LRESULT lRet = 0;

    if (uMsg == WM_NCCREATE)
    {
        LPCREATESTRUCT *pcs = (LPCREATESTRUCT *)lParam;
        fa = (Factory *)(*pcs)->lpCreateParams;

        SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)fa);
        lRet = DefWindowProc(hWnd, uMsg, wParam, lParam);
    }
    else
    {
        fa = (Factory *)(GetWindowLongPtr(hWnd, GWLP_USERDATA));
        if (fa)
        {
            lRet = WndProc(hWnd, uMsg, wParam, lParam);
        }
        else
        {
            lRet = DefWindowProc(hWnd, uMsg, wParam, lParam);
        }
    }
    return lRet;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
    UNREFERENCED_PARAMETER(nCmdShow);
    
    HRESULT hr = CoInitializeEx(NULL,COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    
    if (SUCCEEDED(hr))
    {
    	{	
    		Factory *factory = malloc(sizeof(Factory));
    		*factory = (Factory){ 0 };
    		
    		hr = S_OK;
    		
    		// Create WIC factory
    		hr = CoCreateInstance(	&CLSID_WICImagingFactory,
									NULL, CLSCTX_INPROC_SERVER,
									&IID_IWICImagingFactory,
									IID_PPV_ARGS(&factory->m_pIWICFactory)
									);
			if (SUCCEEDED(hr))
    		{
        		// Create D2D factory
        		hr = D2D1CreateFactory(	D2D1_FACTORY_TYPE_SINGLE_THREADED,
										&IID_ID2D1Factory,
										D2D1_DEBUG_LEVEL_NONE,
										IID_PPV_ARGS(&factory->m_pD2DFactory)
										);
    		}
    		if (SUCCEEDED(hr))
    		{
        		WNDCLASSEX wcex;

        		// Register window class
        		wcex.cbSize        = sizeof(WNDCLASSEX);
        		wcex.style         = CS_HREDRAW | CS_VREDRAW;
        		wcex.lpfnWndProc   = s_WndProc;
        		wcex.cbClsExtra    = 0;
        		wcex.cbWndExtra    = sizeof(LONG_PTR);
        		wcex.hInstance     = hInstance;
        		wcex.hIcon         = NULL;
        		wcex.hCursor       = LoadCursor(NULL, IDC_ARROW);
        		wcex.hbrBackground = NULL;
        		wcex.lpszMenuName  = MAKEINTRESOURCE(IDR_MAINMENU);
        		wcex.lpszClassName = L"WICViewerD2D";
        		wcex.hIconSm       = NULL;

        		factory->m_hInst = hInstance;

        		hr = RegisterClassEx(&wcex) ? S_OK : E_FAIL;
    		}
    		
    		if (SUCCEEDED(hr))
    		{
        		// Create window
        		HWND hWnd = CreateWindow(
            	L"WICViewerD2D",
            	L"WIC Viewer D2D Sample",
            	WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            	CW_USEDEFAULT,
            	CW_USEDEFAULT,
            	640,
            	480,
            	NULL,
            	NULL,
            	hInstance,
            	&factory
            	);

        		hr = hWnd ? S_OK : E_FAIL;
    		}
    		
    		if (SUCCEEDED(hr))
            {
                BOOL fRet;
                MSG msg;

                // Main message loop:
                while ((fRet = GetMessage(&msg, NULL, 0, 0)) != 0)
                {
                    if (fRet == -1)
                    {
                        break;
                    }
                    else
                    {
                        TranslateMessage(&msg);
                        DispatchMessage(&msg);
                    }
                }
            }
		}
		CoUninitialize();
	}
    return 0;
}
