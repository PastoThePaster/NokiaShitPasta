#include <iostream>
#include <Windows.h>
#include "Utils.h"
#include "d3d.h"
#include <dwmapi.h>
#include <vector>
#include <sstream>
#include <string>
#include <algorithm>
#include <list>
#include <filesystem>
#include "offsets.h"
#include "settings.h"
#include "xorstr.hpp"
#include <io.h>

static HWND Window = NULL;
IDirect3D9Ex* pObject = NULL;
static LPDIRECT3DDEVICE9 D3DDevice = NULL;
static LPDIRECT3DVERTEXBUFFER9 VertBuff = NULL;

#define OFFSET_UWORLD 0x9645cc0
int localplayerID;

int opacity = 150;

float Aim_Speed = 6.9f;
const int Max_Aim_Speed = 7;
const int Min_Aim_Speed = 0;


ImFont* m_pFont;

DWORD_PTR Uworld;
DWORD_PTR LocalPawn;
DWORD_PTR Localplayer;
DWORD_PTR Rootcomp;
DWORD_PTR PlayerController;
DWORD_PTR Ulevel;
DWORD_PTR entityx;
bool isaimbotting;

Vector3 localactorpos;
Vector3 Localcam;

static void WindowMain();
static void InitializeD3D();
static void Loop();
static void ShutDown();

FTransform GetBoneIndex(DWORD_PTR mesh, int index)
{
	DWORD_PTR bonearray = read<DWORD_PTR>(DrverInit, FNProcID, mesh + 0x4A8);  // 4A8  changed often 4u

	if (bonearray == NULL) // added 4u
	{
		bonearray = read<DWORD_PTR>(DrverInit, FNProcID, mesh + 0x4A8 + 0x10); // added 4u
	}

	return read<FTransform>(DrverInit, FNProcID, bonearray + (index * 0x30));  // doesn't change
}

Vector3 GetBoneWithRotation(DWORD_PTR mesh, int id)
{
	FTransform bone = GetBoneIndex(mesh, id);
	FTransform ComponentToWorld = read<FTransform>(DrverInit, FNProcID, mesh + 0x1C0);  // have never seen this change 4u

	D3DMATRIX Matrix;
	Matrix = MatrixMultiplication(bone.ToMatrixWithScale(), ComponentToWorld.ToMatrixWithScale());

	return Vector3(Matrix._41, Matrix._42, Matrix._43);
}

D3DMATRIX Matrix(Vector3 rot, Vector3 origin = Vector3(0, 0, 0))
{
	float radPitch = (rot.x * float(M_PI) / 180.f);
	float radYaw = (rot.y * float(M_PI) / 180.f);
	float radRoll = (rot.z * float(M_PI) / 180.f);

	float SP = sinf(radPitch);
	float CP = cosf(radPitch);
	float SY = sinf(radYaw);
	float CY = cosf(radYaw);
	float SR = sinf(radRoll);
	float CR = cosf(radRoll);

	D3DMATRIX matrix;
	matrix.m[0][0] = CP * CY;
	matrix.m[0][1] = CP * SY;
	matrix.m[0][2] = SP;
	matrix.m[0][3] = 0.f;

	matrix.m[1][0] = SR * SP * CY - CR * SY;
	matrix.m[1][1] = SR * SP * SY + CR * CY;
	matrix.m[1][2] = -SR * CP;
	matrix.m[1][3] = 0.f;

	matrix.m[2][0] = -(CR * SP * CY + SR * SY);
	matrix.m[2][1] = CY * SR - CR * SP * SY;
	matrix.m[2][2] = CR * CP;
	matrix.m[2][3] = 0.f;

	matrix.m[3][0] = origin.x;
	matrix.m[3][1] = origin.y;
	matrix.m[3][2] = origin.z;
	matrix.m[3][3] = 1.f;

	return matrix;
}

//4u note:  changes to projectw2s and camera are the most diffucult changes to understand reworking old camloc, be careful blindly making edits

extern Vector3 CameraEXT(0, 0, 0);
float FovAngle;
Vector3 ProjectWorldToScreen(Vector3 WorldLocation, Vector3 camrot)
{
	Vector3 Screenlocation = Vector3(0, 0, 0);
	Vector3 Camera;

	auto chain69 = read<uintptr_t>(DrverInit, FNProcID, Localplayer + 0xa8);
	uint64_t chain699 = read<uintptr_t>(DrverInit, FNProcID, chain69 + 8);

	Camera.x = read<float>(DrverInit, FNProcID, chain699 + 0x7F8);  //camera pitch  watch out for x and y swapped 4u
	Camera.y = read<float>(DrverInit, FNProcID, Rootcomp + 0x12C);  //camera yaw

	float test = asin(Camera.x);
	float degrees = test * (180.0 / M_PI);
	Camera.x = degrees;

	if (Camera.y < 0)
		Camera.y = 360 + Camera.y;

	D3DMATRIX tempMatrix = Matrix(Camera);
	Vector3 vAxisX, vAxisY, vAxisZ;

	vAxisX = Vector3(tempMatrix.m[0][0], tempMatrix.m[0][1], tempMatrix.m[0][2]);
	vAxisY = Vector3(tempMatrix.m[1][0], tempMatrix.m[1][1], tempMatrix.m[1][2]);
	vAxisZ = Vector3(tempMatrix.m[2][0], tempMatrix.m[2][1], tempMatrix.m[2][2]);

	uint64_t chain = read<uint64_t>(DrverInit, FNProcID, Localplayer + 0x70);
	uint64_t chain1 = read<uint64_t>(DrverInit, FNProcID, chain + 0x98);
	uint64_t chain2 = read<uint64_t>(DrverInit, FNProcID, chain1 + 0x130);

	Vector3 vDelta = WorldLocation - read<Vector3>(DrverInit, FNProcID, chain2 + 0x10); //camera location credits for Object9999
	Vector3 vTransformed = Vector3(vDelta.Dot(vAxisY), vDelta.Dot(vAxisZ), vDelta.Dot(vAxisX));

	if (vTransformed.z < 1.f)
		vTransformed.z = 1.f;

	float zoom = read<float>(DrverInit, FNProcID, chain699 + 0x590);

	FovAngle = 80.0f / (zoom / 1.19f);
	float ScreenCenterX = Width / 2.0f;
	float ScreenCenterY = Height / 2.0f;

	Screenlocation.x = ScreenCenterX + vTransformed.x * (ScreenCenterX / tanf(FovAngle * (float)M_PI / 360.f)) / vTransformed.z;
	Screenlocation.y = ScreenCenterY - vTransformed.y * (ScreenCenterX / tanf(FovAngle * (float)M_PI / 360.f)) / vTransformed.z;
	CameraEXT = Camera;

	return Screenlocation;
}


DWORD GUI(LPVOID in)
{
	while (1)
	{
		static bool pressed = false;

		if (GetKeyState(VK_INSERT) & 0x8000)
			pressed = true;

		else if (!(GetKeyState(VK_INSERT) & 0x8000) && pressed) {
			Menu = !Menu;
			pressed = false;
		}
	}
}


typedef struct
{
	DWORD R;
	DWORD G;
	DWORD B;
	DWORD A;
}RGBA;

class Color
{
public:
	RGBA red = { 255,0,0,255 };
	RGBA Magenta = { 255,0,255,255 };
	RGBA yellow = { 255,255,0,255 };
	RGBA grayblue = { 128,128,255,255 };
	RGBA green = { 128,224,0,255 };
	RGBA darkgreen = { 0,224,128,255 };
	RGBA brown = { 192,96,0,255 };
	RGBA pink = { 255,168,255,255 };
	RGBA DarkYellow = { 216,216,0,255 };
	RGBA SilverWhite = { 236,236,236,255 };
	RGBA purple = { 144,0,255,255 };
	RGBA Navy = { 88,48,224,255 };
	RGBA skyblue = { 0,136,255,255 };
	RGBA graygreen = { 128,160,128,255 };
	RGBA blue = { 0,96,192,255 };
	RGBA orange = { 255,128,0,255 };
	RGBA peachred = { 255,80,128,255 };
	RGBA reds = { 255,128,192,255 };
	RGBA darkgray = { 96,96,96,255 };
	RGBA Navys = { 0,0,128,255 };
	RGBA darkgreens = { 0,128,0,255 };
	RGBA darkblue = { 0,128,128,255 };
	RGBA redbrown = { 128,0,0,255 };
	RGBA purplered = { 128,0,128,255 };
	RGBA greens = { 0,255,0,255 };
	RGBA envy = { 0,255,255,255 };
	RGBA black = { 0,0,0,255 };
	RGBA gray = { 128,128,128,255 };
	RGBA white = { 255,255,255,255 };
	RGBA blues = { 30,144,255,255 };
	RGBA lightblue = { 135,206,250,160 };
	RGBA Scarlet = { 220, 20, 60, 160 };
	RGBA white_ = { 255,255,255,200 };
	RGBA gray_ = { 128,128,128,200 };
	RGBA black_ = { 0,0,0,200 };
	RGBA red_ = { 255,0,0,200 };
	RGBA Magenta_ = { 255,0,255,200 };
	RGBA yellow_ = { 255,255,0,200 };
	RGBA grayblue_ = { 128,128,255,200 };
	RGBA green_ = { 128,224,0,200 };
	RGBA darkgreen_ = { 0,224,128,200 };
	RGBA brown_ = { 192,96,0,200 };
	RGBA pink_ = { 255,168,255,200 };
	RGBA darkyellow_ = { 216,216,0,200 };
	RGBA silverwhite_ = { 236,236,236,200 };
	RGBA purple_ = { 144,0,255,200 };
	RGBA Blue_ = { 88,48,224,200 };
	RGBA skyblue_ = { 0,136,255,200 };
	RGBA graygreen_ = { 128,160,128,200 };
	RGBA blue_ = { 0,96,192,200 };
	RGBA orange_ = { 255,128,0,200 };
	RGBA pinks_ = { 255,80,128,200 };
	RGBA Fuhong_ = { 255,128,192,200 };
	RGBA darkgray_ = { 96,96,96,200 };
	RGBA Navy_ = { 0,0,128,200 };
	RGBA darkgreens_ = { 0,128,0,200 };
	RGBA darkblue_ = { 0,128,128,200 };
	RGBA redbrown_ = { 128,0,0,200 };
	RGBA purplered_ = { 128,0,128,200 };
	RGBA greens_ = { 0,255,0,200 };
	RGBA envy_ = { 0,255,255,200 };

	RGBA glassblack = { 0, 0, 0, 160 };
	RGBA GlassBlue = { 65,105,225,80 };
	RGBA glassyellow = { 255,255,0,160 };
	RGBA glass = { 200,200,200,60 };


	RGBA Plum = { 221,160,221,160 };

};
Color Col;


std::string GetGNamesByObjID(int32_t ObjectID)
{
	return 0;
}


typedef struct _FNlEntity
{
	uint64_t Actor;
	int ID;
	uint64_t mesh;
}FNlEntity;

std::vector<FNlEntity> entityList;

#define DEBUG



HWND GameWnd = NULL;
void Window2Target()
{
	while (true)
	{
		if (hwnd)
		{
			ZeroMemory(&ProcessWH, sizeof(ProcessWH));
			GetWindowRect(hwnd, &ProcessWH);
			Width = ProcessWH.right - ProcessWH.left;
			Height = ProcessWH.bottom - ProcessWH.top;
			DWORD dwStyle = GetWindowLong(hwnd, GWL_STYLE);

			if (dwStyle & WS_BORDER)
			{
				ProcessWH.top += 32;
				Height -= 39;
			}
			ScreenCenterX = Width / 2;
			ScreenCenterY = Height / 2;
			MoveWindow(Window, ProcessWH.left, ProcessWH.top, Width, Height, true);
		}
		else
		{
			exit(0);
		}
	}
}


const MARGINS Margin = { -1 };

void WindowMain()
{

	CreateThread(0, 0, (LPTHREAD_START_ROUTINE)Window2Target, 0, 0, 0);
	WNDCLASSEX wClass =
	{
		sizeof(WNDCLASSEX),
		0,
		WindowProc,
		0,
		0,
		nullptr,
		LoadIcon(nullptr, IDI_APPLICATION),
		LoadCursor(nullptr, IDC_ARROW),
		nullptr,
		nullptr,
		TEXT("Test1"),
		LoadIcon(nullptr, IDI_APPLICATION)
	};

	if (!RegisterClassEx(&wClass))
		exit(1);

	hwnd = FindWindowW(NULL, TEXT("Fortnite  "));

	if (hwnd)
	{
		GetClientRect(hwnd, &ProcessWH);
		POINT xy;
		ClientToScreen(hwnd, &xy);
		ProcessWH.left = xy.x;
		ProcessWH.top = xy.y;


		Width = ProcessWH.right;
		Height = ProcessWH.bottom;
	}
	

	Window = CreateWindowExA(NULL, (XorStr("Test1").c_str()), (XorStr("Test1").c_str()), WS_POPUP | WS_VISIBLE, ProcessWH.left, ProcessWH.top, Width, Height, NULL, NULL, 0, NULL);
	DwmExtendFrameIntoClientArea(Window, &Margin);
	SetWindowLong(Window, GWL_EXSTYLE, WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW);
	ShowWindow(Window, SW_SHOW);
	UpdateWindow(Window);

}

void InitializeD3D()
{
	if (FAILED(Direct3DCreate9Ex(D3D_SDK_VERSION, &pObject)))
		exit(3);

	ZeroMemory(&d3d, sizeof(d3d));
	d3d.BackBufferWidth = Width;
	d3d.BackBufferHeight = Height;
	d3d.BackBufferFormat = D3DFMT_A8R8G8B8;
	d3d.MultiSampleQuality = D3DMULTISAMPLE_NONE;
	d3d.AutoDepthStencilFormat = D3DFMT_D16;
	d3d.SwapEffect = D3DSWAPEFFECT_DISCARD;
	d3d.EnableAutoDepthStencil = TRUE;
	d3d.hDeviceWindow = Window;
	d3d.Windowed = TRUE;

	pObject->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, Window, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3d, &D3DDevice);

	IMGUI_CHECKVERSION();

	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = nullptr;
	(void)io;

	ImGui_ImplWin32_Init(Window);
	ImGui_ImplDX9_Init(D3DDevice);

	pObject->Release();
}

bool firstS = false;
Vector3 Camera(unsigned __int64 RootComponent)
{
	unsigned __int64 PtrPitch;
	Vector3 Camera;

	auto pitch = read<uintptr_t>(DrverInit, FNProcID, Offsets::LocalPlayer + 0xb0);
	Camera.x = read<float>(DrverInit, FNProcID, RootComponent + 0x12C);
	Camera.y = read<float>(DrverInit, FNProcID, pitch + 0x678);

	float test = asin(Camera.y);
	float degrees = test * (180.0 / M_PI);

	Camera.y = degrees;

	if (Camera.x < 0)
		Camera.x = 360 + Camera.x;

	return Camera;
}

bool GetAimKey()
{
	return (GetAsyncKeyState(VK_RBUTTON));
}

void aimbot(float x, float y)
{
		float ScreenCenterX = (Width / 2);
		float ScreenCenterY = (Height / 2);
		float TargetX = 0;
		float TargetY = 0;

		if (x != 0)
		{
			if (x > ScreenCenterX)
			{
				TargetX = -(ScreenCenterX - x);
				TargetX /= smoothing;
				if (TargetX + ScreenCenterX > ScreenCenterX * 2) TargetX = 0;
			}

			if (x < ScreenCenterX)
			{
				TargetX = x - ScreenCenterX;
				TargetX /= smoothing;
				if (TargetX + ScreenCenterX < 0) TargetX = 0;
			}
		}

		if (y != 0)
		{
			if (y > ScreenCenterY)
			{
				TargetY = -(ScreenCenterY - y);
				TargetY /= smoothing;
				if (TargetY + ScreenCenterY > ScreenCenterY * 2) TargetY = 0;
			}

			if (y < ScreenCenterY)
			{
				TargetY = y - ScreenCenterY;
				TargetY /= smoothing;
				if (TargetY + ScreenCenterY < 0) TargetY = 0;
			}
		}

		mouse_event(MOUSEEVENTF_MOVE, static_cast<DWORD>(TargetX), static_cast<DWORD>(TargetY), NULL, NULL);

		return;
}

double GetCrossDistance(double x1, double y1, double x2, double y2)
{
	return sqrt(pow((x2 - x1), 2) + pow((y2 - y1), 2));
}

bool GetClosestPlayerToCrossHair(Vector3 Pos, float& max, float aimfov, DWORD_PTR entity)
{
	if (!GetAimKey() || !isaimbotting)
	{
		if (entity)
		{
			float Dist = GetCrossDistance(Pos.x, Pos.y, Width / 2, Height / 2);

			if (Dist < max)
			{
				max = Dist;
				entityx = entity;
				AimbotFOV = aimfov;
			}
		}
	}
	return false;
}

void AimAt(DWORD_PTR entity)
{
	uint64_t currentactormesh = read<uint64_t>(DrverInit, FNProcID, entity + 0x280);
	auto rootHead = GetBoneWithRotation(currentactormesh, 7);
	Vector3 rootHeadOut = ProjectWorldToScreen(rootHead, Vector3(Localcam.y, Localcam.x, Localcam.z));

	if (rootHeadOut.y != 0 || rootHeadOut.y != 0)
	{
		aimbot(rootHeadOut.x, rootHeadOut.y);
	}
}

void AIms(DWORD_PTR entity, Vector3 Localcam)
{
	float max = 100.f;

	uint64_t currentactormesh = read<uint64_t>(DrverInit, FNProcID, entity + 0x280);  // changed often 

	Vector3 rootHead = GetBoneWithRotation(currentactormesh, 66);
	Vector3 rootHeadOut = ProjectWorldToScreen(rootHead, Vector3(Localcam.y, Localcam.x, Localcam.z));

	if (GetClosestPlayerToCrossHair(rootHeadOut, max, AimbotFOV, entity))
		entityx = entity;
}


float TextShadowNigg(ImFont* pFont, const std::string& text, const ImVec2& pos, float size, ImU32 color, bool center)
{
	

	std::stringstream stream(text);
	std::string line;

	float y = 0.0f;
	int i = 0;

	while (std::getline(stream, line))
	{
		ImVec2 textSize = pFont->CalcTextSizeA(size, FLT_MAX, 0.0f, line.c_str());

		if (center)
		{
			ImGui::GetOverlayDrawList()->AddText(pFont, size, ImVec2((pos.x - textSize.x / 2.0f) + 1, (pos.y + textSize.y * i) + 1), ImGui::GetColorU32(ImVec4(0, 0, 0, 255)), line.c_str());
			ImGui::GetOverlayDrawList()->AddText(pFont, size, ImVec2((pos.x - textSize.x / 2.0f) - 1, (pos.y + textSize.y * i) - 1), ImGui::GetColorU32(ImVec4(0, 0, 0, 255)), line.c_str());

			ImGui::GetOverlayDrawList()->AddText(pFont, size, ImVec2(pos.x - textSize.x / 2.0f, pos.y + textSize.y * i), ImGui::GetColorU32(color), line.c_str());
		}
		else
		{
			ImGui::GetOverlayDrawList()->AddText(pFont, size, ImVec2((pos.x) + 1, (pos.y + textSize.y * i) + 1), ImGui::GetColorU32(ImVec4(0, 0, 0, 255)), line.c_str());
			ImGui::GetOverlayDrawList()->AddText(pFont, size, ImVec2((pos.x) - 1, (pos.y + textSize.y * i) - 1), ImGui::GetColorU32(ImVec4(0, 0, 0, 255)), line.c_str());

			ImGui::GetOverlayDrawList()->AddText(pFont, size, ImVec2(pos.x, pos.y + textSize.y * i), ImGui::GetColorU32(color), line.c_str());
		}

		y = pos.y + textSize.y * (i + 1);
		i++;
	}
	return y;
}

void DrawLine(int x1, int y1, int x2, int y2, RGBA* color, int thickness)
{
	ImGui::GetOverlayDrawList()->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), ImGui::ColorConvertFloat4ToU32(ImVec4(color->R / 255.0, color->G / 255.0, color->B / 255.0, color->A / 255.0)), thickness);
}

void DrawFilledRect(int x, int y, int w, int h, RGBA* color)
{
	ImGui::GetOverlayDrawList()->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), ImGui::ColorConvertFloat4ToU32(ImVec4(color->R / 255.0, color->G / 255.0, color->B / 255.0, color->A / 255.0)), 0, 0);
}

std::string GetObjectNames(int32_t ObjectID) /* maybe working.. */
{
	uint64_t gname = read<uint64_t>(DrverInit, FNProcID, base_address + 0x9643080);

	int64_t fNamePtr = read<uint64_t>(DrverInit, FNProcID, gname + int(ObjectID / 0x4000) * 0x8);
	int64_t fName = read<uint64_t>(DrverInit, FNProcID, fNamePtr + int(ObjectID % 0x4000) * 0x8);

	char pBuffer[64] = { NULL };

	info_t blyat;
	blyat.pid = FNProcID;
	blyat.address = fName + 0x10;
	blyat.value = &pBuffer;
	blyat.size = sizeof(pBuffer);

	unsigned long int asd;
	DeviceIoControl(DrverInit, ctl_read, &blyat, sizeof blyat, &blyat, sizeof blyat, &asd, nullptr);

	return std::string(pBuffer);
}
void DrawNormalBox(int x, int y, int w, int h, int borderPx, RGBA* color)
{
	DrawFilledRect(x + borderPx, y, w, borderPx, color);
	DrawFilledRect(x + w - w + borderPx, y, w, borderPx, color);
	DrawFilledRect(x, y, borderPx, h, color);
	DrawFilledRect(x, y + h - h + borderPx * 2, borderPx, h, color);
	DrawFilledRect(x + borderPx, y + h + borderPx, w, borderPx, color);
	DrawFilledRect(x + w - w + borderPx, y + h + borderPx, w, borderPx, color);
	DrawFilledRect(x + w + borderPx, y, borderPx, h, color); 
	DrawFilledRect(x + w + borderPx, y + h - h + borderPx * 2, borderPx, h, color);
}

void Background(int x, int y, int w, int h, ImColor color)
{
	ImGui::GetOverlayDrawList()->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), ImGui::ColorConvertFloat4ToU32(ImVec4(color)), 0, 0);
}

void ShadowRGBText(int x, int y, ImColor color, const char* str)
{
	ImFont a;
	std::string utf_8_1 = std::string(str);
	std::string utf_8_2 = string_To_UTF8(utf_8_1);
	ImGui::GetOverlayDrawList()->AddText(ImVec2(x + 1, y + 2), ImGui::ColorConvertFloat4ToU32(ImColor(0, 0, 0, 0)), utf_8_2.c_str());
	ImGui::GetOverlayDrawList()->AddText(ImVec2(x + 1, y + 1), ImGui::ColorConvertFloat4ToU32(ImColor(0, 0, 0, 200)), utf_8_2.c_str());
	ImGui::GetOverlayDrawList()->AddText(ImVec2(x, y), ImGui::ColorConvertFloat4ToU32(ImVec4(color)), utf_8_2.c_str());
}

int ScreenX = GetSystemMetrics(SM_CXSCREEN) / 2;
int ScreenY = GetSystemMetrics(SM_CYSCREEN);


void drawLoop()
{
	Uworld = read<DWORD_PTR>(DrverInit, FNProcID, base_address + OFFSET_UWORLD);

	DWORD_PTR Gameinstance = read<DWORD_PTR>(DrverInit, FNProcID, Uworld + 0x180);

	if (Gameinstance == (DWORD_PTR)nullptr)
		return;


	DWORD_PTR LocalPlayers = read<DWORD_PTR>(DrverInit, FNProcID, Gameinstance + 0x38);

	if (LocalPlayers == (DWORD_PTR)nullptr)
		return;

	Localplayer = read<DWORD_PTR>(DrverInit, FNProcID, LocalPlayers);

	if (Localplayer == (DWORD_PTR)nullptr)
		return;

	PlayerController = read<DWORD_PTR>(DrverInit, FNProcID, Localplayer + 0x30);

	if (PlayerController == (DWORD_PTR)nullptr)
		return;

	LocalPawn = read<uint64_t>(DrverInit, FNProcID, PlayerController + 0x2A0);

	if (LocalPawn == (DWORD_PTR)nullptr)
		return;

	Rootcomp = read<uint64_t>(DrverInit, FNProcID, LocalPawn + 0x130);

	if (Rootcomp == (DWORD_PTR)nullptr)
		return;

	if (LocalPawn != 0) {
		localplayerID = read<int>(DrverInit, FNProcID, LocalPawn + 0x18);
	}

	Ulevel = read<DWORD_PTR>(DrverInit, FNProcID, Uworld + 0x30);

	if (Ulevel == (DWORD_PTR)nullptr)
		return;

	DWORD64 PlayerState = read<DWORD64>(DrverInit, FNProcID, LocalPawn + 0x240);

	if (PlayerState == (DWORD_PTR)nullptr)
		return;

	DWORD ActorCount = read<DWORD>(DrverInit, FNProcID, Ulevel + 0xA0);

	DWORD_PTR AActors = read<DWORD_PTR>(DrverInit, FNProcID, Ulevel + 0x98);

	if (AActors == (DWORD_PTR)nullptr)
		return;

	Vector3 Localcam = Camera(Rootcomp);

	for (int i = 0; i < ActorCount; i++)
	{
		uint64_t CurrentActor = read<uint64_t>(DrverInit, FNProcID, AActors + i * 0x8);

		int curactorid = read<int>(DrverInit, FNProcID, CurrentActor + 0x18);

		if (curactorid == localplayerID || curactorid == 17384284 || curactorid == 9875145 || curactorid == 9873134 || curactorid == 9876800 || curactorid == 9874439)
		{
			if (CurrentActor == (uint64_t)nullptr || CurrentActor == -1 || CurrentActor == NULL)
				continue;

			uint64_t CurrentActorRootComponent = read<uint64_t>(DrverInit, FNProcID, CurrentActor + 0x130);

			if (CurrentActorRootComponent == (uint64_t)nullptr || CurrentActorRootComponent == -1 || CurrentActorRootComponent == NULL)
				continue;

			uint64_t currentactormesh = read<uint64_t>(DrverInit, FNProcID, CurrentActor + 0x280);

			if (currentactormesh == (uint64_t)nullptr || currentactormesh == -1 || currentactormesh == NULL)
				continue;

			int MyTeamId = read<int>(DrverInit, FNProcID, PlayerState + 0xED0);

			DWORD64 otherPlayerState = read<uint64_t>(DrverInit, FNProcID, CurrentActor + 0x240);

			if (otherPlayerState == (uint64_t)nullptr || otherPlayerState == -1 || otherPlayerState == NULL)
				continue;

			int ActorTeamId = read<int>(DrverInit, FNProcID, otherPlayerState + 0xED0);


			Vector3 Headpos = GetBoneWithRotation(currentactormesh, 66);
			Localcam = CameraEXT;
			localactorpos = read<Vector3>(DrverInit, FNProcID, Rootcomp + 0x11C);

			float distance = localactorpos.Distance(Headpos) / 100.f;

			if (distance < 1.5f)

				Vector3 Headpos = GetBoneWithRotation(currentactormesh, 66);
			Localcam = CameraEXT;
			localactorpos = read<Vector3>(DrverInit, FNProcID, Rootcomp + 0x11C);

			if (distance < 0.5)
				continue;
			localactorpos = read<Vector3>(DrverInit, FNProcID, Rootcomp + 0x11C);
			Vector3 rootOut = GetBoneWithRotation(currentactormesh, 0);
			Vector3 Out = ProjectWorldToScreen(rootOut, Vector3(Localcam.y, Localcam.x, Localcam.z));

			Vector3 HeadposW2s = ProjectWorldToScreen(Headpos, Vector3(Localcam.y, Localcam.x, Localcam.z));
			Vector3 bone0 = GetBoneWithRotation(currentactormesh, 0);
			Vector3 bottom = ProjectWorldToScreen(bone0, Vector3(Localcam.y, Localcam.x, Localcam.z));
			Vector3 Headbox = ProjectWorldToScreen(Vector3(Headpos.x, Headpos.y, Headpos.z + 15), Vector3(Localcam.y, Localcam.x, Localcam.z));
			Vector3 Aimpos = ProjectWorldToScreen(Vector3(Headpos.x, Headpos.y, Headpos.z + 10), Vector3(Localcam.y, Localcam.x, Localcam.z));
			Vector3 HeadBone = GetBoneWithRotation(currentactormesh, 96);
			Vector3 RootBone = GetBoneWithRotation(currentactormesh, 0);
			uint64_t TheirRootcomp = read<uint64_t>(DrverInit, FNProcID, CurrentActor + 0x130);
			Vector3 HeadBoneOut = ProjectWorldToScreen(Vector3(HeadBone.x, HeadBone.y, HeadBone.z), Vector3(Localcam.y, Localcam.x, Localcam.z));
			Vector3 RootBoneOut = ProjectWorldToScreen(RootBone, Vector3(Localcam.y, Localcam.x, Localcam.z));
#define vischeck 

			float BoxHeight = abs(HeadBoneOut.y - RootBoneOut.y);
			float BoxWidth = BoxHeight / 3.0f;

			float LeftX = (float)Headbox.x - (BoxWidth / 1);
			float LeftY = (float)bottom.y;

			float CornerHeight = abs(Headbox.y - bottom.y);
			float CornerWidth = CornerHeight * 0.75;

			Vector3 vHeadBone = GetBoneWithRotation(currentactormesh, 96);
			Vector3 vHip = GetBoneWithRotation(currentactormesh, 2);
			Vector3 vNeck = GetBoneWithRotation(currentactormesh, 65);
			Vector3 vUpperArmLeft = GetBoneWithRotation(currentactormesh, 34);
			Vector3 vUpperArmRight = GetBoneWithRotation(currentactormesh, 91);
			Vector3 vLeftHand = GetBoneWithRotation(currentactormesh, 35);
			Vector3 vRightHand = GetBoneWithRotation(currentactormesh, 63);
			Vector3 vLeftHand1 = GetBoneWithRotation(currentactormesh, 33);
			Vector3 vRightHand1 = GetBoneWithRotation(currentactormesh, 60);
			Vector3 vRightThigh = GetBoneWithRotation(currentactormesh, 74);
			Vector3 vLeftThigh = GetBoneWithRotation(currentactormesh, 67);
			Vector3 vRightCalf = GetBoneWithRotation(currentactormesh, 75);
			Vector3 vLeftCalf = GetBoneWithRotation(currentactormesh, 68);
			Vector3 vLeftFoot = GetBoneWithRotation(currentactormesh, 69);
			Vector3 vRightFoot = GetBoneWithRotation(currentactormesh, 76);
			Vector3 Finger1 = GetBoneWithRotation(currentactormesh, BONE_R_FINGER_1);
			Vector3 Finger2 = GetBoneWithRotation(currentactormesh, BONE_R_FINGER_2);
			Vector3 Finger3 = GetBoneWithRotation(currentactormesh, BONE_R_FINGER_3);
			Vector3 FingerL1 = GetBoneWithRotation(currentactormesh, BONE_L_FINGER_1);
			Vector3 FingerL2 = GetBoneWithRotation(currentactormesh, BONE_L_FINGER_2);
			Vector3 FingerL3 = GetBoneWithRotation(currentactormesh, BONE_L_FINGER_3);

			Vector3 vHeadBoneOut = ProjectWorldToScreen(vHeadBone, Vector3(Localcam.y, Localcam.x, Localcam.z));
			Vector3 vHipOut = ProjectWorldToScreen(vHip, Vector3(Localcam.y, Localcam.x, Localcam.z));
			Vector3 vNeckOut = ProjectWorldToScreen(vNeck, Vector3(Localcam.y, Localcam.x, Localcam.z));
			Vector3 vUpperArmLeftOut = ProjectWorldToScreen(vUpperArmLeft, Vector3(Localcam.y, Localcam.x, Localcam.z));
			Vector3 vUpperArmRightOut = ProjectWorldToScreen(vUpperArmRight, Vector3(Localcam.y, Localcam.x, Localcam.z));
			Vector3 vLeftHandOut = ProjectWorldToScreen(vLeftHand, Vector3(Localcam.y, Localcam.x, Localcam.z));
			Vector3 vRightHandOut = ProjectWorldToScreen(vRightHand, Vector3(Localcam.y, Localcam.x, Localcam.z));
			Vector3 vLeftHandOut1 = ProjectWorldToScreen(vLeftHand1, Vector3(Localcam.y, Localcam.x, Localcam.z));
			Vector3 vRightHandOut1 = ProjectWorldToScreen(vRightHand1, Vector3(Localcam.y, Localcam.x, Localcam.z));
			Vector3 vRightThighOut = ProjectWorldToScreen(vRightThigh, Vector3(Localcam.y, Localcam.x, Localcam.z));
			Vector3 vLeftThighOut = ProjectWorldToScreen(vLeftThigh, Vector3(Localcam.y, Localcam.x, Localcam.z));
			Vector3 vRightCalfOut = ProjectWorldToScreen(vRightCalf, Vector3(Localcam.y, Localcam.x, Localcam.z));
			Vector3 vLeftCalfOut = ProjectWorldToScreen(vLeftCalf, Vector3(Localcam.y, Localcam.x, Localcam.z));
			Vector3 vLeftFootOut = ProjectWorldToScreen(vLeftFoot, Vector3(Localcam.y, Localcam.x, Localcam.z));
			Vector3 vRightFootOut = ProjectWorldToScreen(vRightFoot, Vector3(Localcam.y, Localcam.x, Localcam.z));
			Vector3 Finger1R = ProjectWorldToScreen(Finger1, Vector3(Localcam.y, Localcam.x, Localcam.z));
			Vector3 Finger2R = ProjectWorldToScreen(Finger2, Vector3(Localcam.y, Localcam.x, Localcam.z));
			Vector3 Finger3R = ProjectWorldToScreen(Finger3, Vector3(Localcam.y, Localcam.x, Localcam.z));
			Vector3 Finger1L = ProjectWorldToScreen(FingerL1, Vector3(Localcam.y, Localcam.x, Localcam.z));
			Vector3 Finger2L = ProjectWorldToScreen(FingerL2, Vector3(Localcam.y, Localcam.x, Localcam.z));
			Vector3 Finger3L = ProjectWorldToScreen(FingerL3, Vector3(Localcam.y, Localcam.x, Localcam.z));

			float closestDistance = FLT_MAX;
			DWORD_PTR closestPawn = NULL;

			bool bIsDying = read<bool>(DrverInit, FNProcID, CurrentActor + 0x520);
			if (ActorTeamId != MyTeamId && bIsDying)
			{
				float snaplinePower = 1.0f;
				if (Lines)
				{
					DrawLine(ScreenX, ScreenY, bottom.x, bottom.y, &Col.black, snaplinePower);
				}

				if (PlayerESP)
				{
					ImGui::GetOverlayDrawList()->AddRectFilled(ImVec2(LeftX, LeftY), ImVec2(HeadposW2s.x + BoxWidth, HeadposW2s.y + 5.0f), IM_COL32(0, 0, 0, opacity));
					ImGui::GetOverlayDrawList()->AddRect(ImVec2(LeftX, LeftY), ImVec2(HeadposW2s.x + BoxWidth, HeadposW2s.y + 5.0f), IM_COL32(255, 0, 0, 255));
				}

				if (Distance)
				{
					char dist2[64];
					if (Player)
					{
						sprintf_s(dist2, "      [%.fm]", distance);
					}
					else
					{
						sprintf_s(dist2, "  [%.fm]", distance);
					}
					ShadowRGBText(HeadposW2s.x - 35, HeadposW2s.y - 15, ImColor(255, 0, 0, 200), dist2);
				}

				if (Player)
				{
					char dist[64];
					if (Distance)
					{
						sprintf_s(dist, "Nigerian", distance);
					}
					else
					{
						sprintf_s(dist, "  Nigerian", distance);
					}
					ShadowRGBText(HeadposW2s.x - 35, HeadposW2s.y - 15, ImColor(255, 0, 0, 200), dist);
				}

				auto dx = HeadposW2s.x - (Width / 2);
				auto dy = HeadposW2s.y - (Height / 2);
				auto dist = sqrtf(dx * dx + dy * dy);

				if (dist < AimbotFOV && dist < closestDistance) {
					closestDistance = dist;
					closestPawn = CurrentActor;
				}
			}

			if (MouseAimbot)
			{
				if (MouseAimbot && closestPawn && GetAsyncKeyState(VK_RBUTTON) < 0) {
					AimAt(closestPawn);
				}
			}
		}
	}
}

int r, g, b;
int r1, g2, b2;

float color_red = 1.;
float color_green = 0;
float color_blue = 0;
float color_random = 0.0;
float color_speed = -10.0;
bool rainbowmode = false;

void ColorChange()
{
	if (rainbowmode)
	{
		static float Color[3];
		static DWORD Tickcount = 0;
		static DWORD Tickcheck = 0;
		ImGui::ColorConvertRGBtoHSV(color_red, color_green, color_blue, Color[0], Color[1], Color[2]);
		if (GetTickCount() - Tickcount >= 1)
		{
			if (Tickcheck != Tickcount)
			{
				Color[0] += 0.001f * color_speed;
				Tickcheck = Tickcount;
			}
			Tickcount = GetTickCount();
		}
		if (Color[0] < 0.0f) Color[0] += 1.0f;
		ImGui::ColorConvertHSVtoRGB(Color[0], Color[1], Color[2], color_red, color_green, color_blue);
	}
}

void Render() {
	ImGui_ImplDX9_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	int X;
	int Y;
	float size1 = 3.0f;
	float size2 = 2.0f;

	if (AimbotCircle)
	{
		ImGui::GetOverlayDrawList()->AddCircle(ImVec2(ScreenCenterX, ScreenCenterY), AimbotFOV + 1, ImColor(0, 0, 0, 255), Roughness);
		ImGui::GetOverlayDrawList()->AddCircle(ImVec2(ScreenCenterX, ScreenCenterY), AimbotFOV, ImColor(255, 255, 255, 255), Roughness);
	}



	if (Crosshair)
	{
		ImGui::GetOverlayDrawList()->AddLine(ImVec2(Width / 2 - 12, Height / 2), ImVec2(Width / 2 - 2, Height / 2), ImGui::GetColorU32({ 14, 14, 14, 255.f }), 2.0f);
		ImGui::GetOverlayDrawList()->AddLine(ImVec2(Width / 2 + 13, Height / 2), ImVec2(Width / 2 + 3, Height / 2), ImGui::GetColorU32({ 14, 14, 14, 255.f }), 2.0f);
		ImGui::GetOverlayDrawList()->AddLine(ImVec2(Width / 2, Height / 2 - 12), ImVec2(Width / 2, Height / 2 - 2), ImGui::GetColorU32({ 14, 14, 14, 255.f }), 2.0f);
		ImGui::GetOverlayDrawList()->AddLine(ImVec2(Width / 2, Height / 2 + 13), ImVec2(Width / 2, Height / 2 + 3), ImGui::GetColorU32({ 14, 14, 14, 255.f }), 2.0f);
	}

	ColorChange();

	bool circleedit = false;

	ImVec4* colors = ImGui::GetStyle().Colors;
	colors[ImGuiCol_Text] = ImVec4(0.80f, 0.80f, 0.80f, 1.00f);
	colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
	colors[ImGuiCol_WindowBg] = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
	colors[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
	colors[ImGuiCol_PopupBg] = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
	colors[ImGuiCol_Border] = ImVec4(0.43f, 0.43f, 0.50f, 1.00f);
	colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
	colors[ImGuiCol_FrameBg] = ImVec4(0.23f, 0.23f, 0.23f, 1.00f);
	colors[ImGuiCol_FrameBgHovered] = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
	colors[ImGuiCol_FrameBgActive] = ImVec4(0.52f, 0.52f, 0.52f, 1.00f);
	colors[ImGuiCol_TitleBg] = ImVec4(0.22f, 0.21f, 0.21f, 1.00f);
	colors[ImGuiCol_TitleBgActive] = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
	colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
	colors[ImGuiCol_MenuBarBg] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
	colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02f, 0.02f, 0.02f, 1.00f);
	colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.31f, 0.31f, 0.31f, 1.00f);
	colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.41f, 0.41f, 0.41f, 1.00f);
	colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);
	colors[ImGuiCol_CheckMark] = ImVec4(0.80f, 0.80f, 0.80f, 1.00f);
	colors[ImGuiCol_SliderGrab] = ImVec4(0.80f, 0.80f, 0.80f, 1.00f);
	colors[ImGuiCol_SliderGrabActive] = ImVec4(0.86f, 0.86f, 0.86f, 1.00f);
	colors[ImGuiCol_Button] = ImVec4(0.23f, 0.23f, 0.23f, 1.00f);
	colors[ImGuiCol_ButtonHovered] = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
	colors[ImGuiCol_ButtonActive] = ImVec4(0.52f, 0.52f, 0.52f, 1.00f);
	colors[ImGuiCol_Header] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
	colors[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
	colors[ImGuiCol_HeaderActive] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
	colors[ImGuiCol_Separator] = ImVec4(0.43f, 0.43f, 0.50f, 1.00f);
	colors[ImGuiCol_SeparatorHovered] = ImVec4(0.10f, 0.40f, 0.75f, 1.00f);
	colors[ImGuiCol_SeparatorActive] = ImVec4(0.10f, 0.40f, 0.75f, 1.00f);
	colors[ImGuiCol_ResizeGrip] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
	colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
	colors[ImGuiCol_ResizeGripActive] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
	colors[ImGuiCol_Tab] = ImVec4(0.18f, 0.35f, 0.58f, 1.00f);
	colors[ImGuiCol_TabHovered] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
	colors[ImGuiCol_TabActive] = ImVec4(0.20f, 0.41f, 0.68f, 1.00f);
	colors[ImGuiCol_TabUnfocused] = ImVec4(0.07f, 0.10f, 0.15f, 1.00f);
	colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.14f, 0.26f, 0.42f, 1.00f);
	colors[ImGuiCol_PlotLines] = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
	colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
	colors[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
	colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
	colors[ImGuiCol_TextSelectedBg] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
	colors[ImGuiCol_DragDropTarget] = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
	colors[ImGuiCol_NavHighlight] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
	colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
	colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.97f);
	colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 1.00f);
	ImGuiStyle* style = &ImGui::GetStyle();
	style->WindowRounding = 0.0f;
	style->FrameRounding = 0.0f;
	style->PopupRounding = 0.0f;
	style->GrabRounding = 0.0f;

	//style->WindowTitleAlign = ImVec2(1.0f, 0.5f);


	ImGui::SetNextWindowSize({ 360, 500 });
	if (Menu)
	{
		ImGui::Begin(XorStr("Fortnite [INSERT TO TOGGLE MENU]").c_str(), 0, ImGuiWindowFlags_::ImGuiWindowFlags_NoResize | ImGuiWindowFlags_::ImGuiWindowFlags_NoCollapse);
		ImGui::Text(XorStr("Aim ").c_str());
		ImGui::Text(XorStr("  ").c_str());

		ImGui::Checkbox(XorStr("Aimbot").c_str(), &MouseAimbot);
		//ImGui::Combo(XorStr("Aim Bone").c_str(), &Settings::hitboxpos, hitboxes, sizeof(hitboxes) / sizeof(*hitboxes));
		ImGui::Checkbox(XorStr("Show FOV").c_str(), &AimbotCircle);

		if (AimbotCircle)
		{
			ImGui::SliderFloat(XorStr("FOV Size").c_str(), &AimbotFOV, 15, 350);
		}

		ImGui::SliderFloat(XorStr("Aim Smoothing").c_str(), &smoothing, 1, 25);
		ImGui::Checkbox(XorStr("Crosshair").c_str(), &Crosshair);

		ImGui::Text("  ");
		ImGui::Text(XorStr("ESP ").c_str());
		ImGui::Text("  ");

		ImGui::Checkbox(XorStr("Box ESP").c_str(), &PlayerESP);
		if (PlayerESP)
		{
			ImGui::SliderInt(XorStr("Box Opacity").c_str(), &opacity, 0, 255);
		}
		ImGui::Checkbox(XorStr("Snaplines").c_str(), &Lines);
		ImGui::Checkbox(XorStr("Player ESP").c_str(), &Player);
		ImGui::Checkbox(XorStr("Distance ESP").c_str(), &Distance);

		ImGui::End();
	}
	
	drawLoop();

	ImGui::EndFrame();
	D3DDevice->SetRenderState(D3DRS_ZENABLE, false);
	D3DDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, false);
	D3DDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, false);
	D3DDevice->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.0f, 0);

	if (D3DDevice->BeginScene() >= 0)
	{
		ImGui::Render();
		ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
		D3DDevice->EndScene();
	}
	HRESULT Results = D3DDevice->Present(NULL, NULL, NULL, NULL);

	if (Results == D3DERR_DEVICELOST && D3DDevice->TestCooperativeLevel() == D3DERR_DEVICENOTRESET)
	{
		ImGui_ImplDX9_InvalidateDeviceObjects();
		D3DDevice->Reset(&d3d);
		ImGui_ImplDX9_CreateDeviceObjects();
	}
}

DWORD Menuthread(LPVOID in)
{
	while (1)
	{
		HWND test = FindWindowA(0, (XorStr("Fortnite  ").c_str()));

		if (test == NULL)
		{
			ExitProcess(0);
		}
	}
}


MSG Message_Loop = { NULL };

void Loop()
{
	static RECT old_rc;
	ZeroMemory(&Message_Loop, sizeof(MSG));

	while (Message_Loop.message != WM_QUIT)
	{
		if (PeekMessage(&Message_Loop, Window, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&Message_Loop);
			DispatchMessage(&Message_Loop);
		}

		HWND hwnd_active = GetForegroundWindow();

		if (hwnd_active == hwnd) {
			HWND hwndtest = GetWindow(hwnd_active, GW_HWNDPREV);
			SetWindowPos(Window, hwndtest, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		}

		if (GetAsyncKeyState(0x23) & 1)
			exit(8);

		RECT rc;
		POINT xy;

		ZeroMemory(&rc, sizeof(RECT));
		ZeroMemory(&xy, sizeof(POINT));
		GetClientRect(hwnd, &rc);
		ClientToScreen(hwnd, &xy);
		rc.left = xy.x;
		rc.top = xy.y;

		ImGuiIO& io = ImGui::GetIO();
		io.ImeWindowHandle = hwnd;
		io.DeltaTime = 1.0f / 60.0f;

		POINT p;
		GetCursorPos(&p);
		io.MousePos.x = p.x - xy.x;
		io.MousePos.y = p.y - xy.y;

		if (GetAsyncKeyState(VK_LBUTTON)) {
			io.MouseDown[0] = true;
			io.MouseClicked[0] = true;
			io.MouseClickedPos[0].x = io.MousePos.x;
			io.MouseClickedPos[0].x = io.MousePos.y;
		}
		else
			io.MouseDown[0] = false;

		if (rc.left != old_rc.left || rc.right != old_rc.right || rc.top != old_rc.top || rc.bottom != old_rc.bottom)
		{
			old_rc = rc;

			Width = rc.right;
			Height = rc.bottom;

			d3d.BackBufferWidth = Width;
			d3d.BackBufferHeight = Height;
			SetWindowPos(Window, (HWND)0, xy.x, xy.y, Width, Height, SWP_NOREDRAW);
			D3DDevice->Reset(&d3d);
		}
		Render();
	}
	ImGui_ImplDX9_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	DestroyWindow(Window);
}

LRESULT CALLBACK WindowProc(HWND hWnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hWnd, Message, wParam, lParam))
		return true;

	switch (Message)
	{
	case WM_DESTROY:
		ShutDown();
		PostQuitMessage(0);
		exit(4);
		break;
	case WM_SIZE:
		if (D3DDevice != NULL && wParam != SIZE_MINIMIZED)
		{
			ImGui_ImplDX9_InvalidateDeviceObjects();
			d3d.BackBufferWidth = LOWORD(lParam);
			d3d.BackBufferHeight = HIWORD(lParam);
			HRESULT hr = D3DDevice->Reset(&d3d);
			if (hr == D3DERR_INVALIDCALL)
				IM_ASSERT(0);
			ImGui_ImplDX9_CreateDeviceObjects();
		}
		break;
	default:
		return DefWindowProc(hWnd, Message, wParam, lParam);
		break;
	}
	return 0;
}


void ShutDown()
{
	VertBuff->Release();
	D3DDevice->Release();
	pObject->Release();

	DestroyWindow(Window);
	UnregisterClass(XorStr(L"fgers").c_str(), NULL);
}

int main(int argc, const char* argv[])
{
	ShowWindow(GetConsoleWindow(), SW_HIDE);
	Sleep(300);

	CreateThread(NULL, NULL, GUI, NULL, NULL, NULL);

	DrverInit = CreateFileW((XorStr(L"\\\\.\\a1SSLSubWo32").c_str()), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

	if (DrverInit == INVALID_HANDLE_VALUE)
	{
		exit(0);
	}

	info_t Input_Output_Data1;
	unsigned long int Readed_Bytes_Amount1;
	DeviceIoControl(DrverInit, ctl_clear, &Input_Output_Data1, sizeof Input_Output_Data1, &Input_Output_Data1, sizeof Input_Output_Data1, &Readed_Bytes_Amount1, nullptr);


	while (hwnd == NULL)
	{
		hwnd = FindWindowA(0, (XorStr("Fortnite  ").c_str()));
		Sleep(300);

	}
	GetWindowThreadProcessId(hwnd, &FNProcID);

	
	info_t Input_Output_Data;
	Input_Output_Data.pid = FNProcID;
	unsigned long int Readed_Bytes_Amount;
	CreateThread(NULL, NULL, Menuthread, NULL, NULL, NULL);
	DeviceIoControl(DrverInit, ctl_base, &Input_Output_Data, sizeof Input_Output_Data, &Input_Output_Data, sizeof Input_Output_Data, &Readed_Bytes_Amount, nullptr);

	WindowMain();
	InitializeD3D();

	Loop();
	ShutDown();

	return 0;

}