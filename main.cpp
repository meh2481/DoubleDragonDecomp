#include "wfLZ.h"
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <stdlib.h>
#include <cstdio>
#include <squish.h>
#ifdef _WIN32
	#include <windows.h>
#endif
#include "FreeImage.h"
#include <list>
#include <cmath>
#include <cstring>
using namespace std;

int g_DecompressFlags;
bool g_bSeparate;
bool g_bPieceTogether;
bool g_bColOnly;
bool g_bMulOnly;

//Top of file
typedef struct
{
	uint32_t unkOffset1;
	uint32_t unknown1;
	uint32_t numImages;
	uint32_t numUnknown1;
	uint32_t unknown2; 	//Always 0x00 01 A0 01 ?
	uint32_t frameHeaderOffset;	//point to frameHeader
}anbHeader;

//Repeat for anbHeader.numImages
typedef struct
{
	uint32_t texDescOffset;	//Point to texDesc
}frameHeader;

typedef struct
{
	uint32_t minx;
	uint32_t maxx;
	uint32_t miny;
	uint32_t maxy;
	uint32_t img_offset;	//Point to texHeader
	uint32_t unk1;			//Probably image size or something
	uint32_t pieceOffset;	//Point to PiecesDesc
}texDesc;

typedef struct
{
	uint32_t numPieces;
	//piece[]	//Followed by numPieces pieces
} PiecesDesc;

typedef struct
{
	uint32_t type;
	uint32_t width;
	uint32_t height;
	uint32_t unknown1[5];
	//uint8_t data[]
} texHeader;

/*typedef struct  
{
	int      unknown0[4];
	uint64_t texOffset; // offset from start of file to TexDesc
	int      texDataSize;
	int      unknown1;
	uint64_t pieceOffset; // offset from start of file to PiecesDesc
} FrameDesc;*/

typedef struct
{
	float x;
	float y;
} Vec2;

typedef struct 
{
	Vec2 topLeft;
	Vec2 topLeftUV;
	Vec2 bottomRight;
	Vec2 bottomRightUV;
} piece;

typedef struct
{
	uint8_t r;
	uint8_t g;
	uint8_t b;
	uint8_t a;
} pixel;

#define PALETTE_SIZE					256

#define TEXTURE_TYPE_RAW				0	//No additional compression
#define TEXTURE_TYPE_256_COL			1	//256-color 4bpp palette followed by pixel data
#define TEXTURE_TYPE_DXT1_COL			2	//squish::kDxt1 color, no multiply
#define TEXTURE_TYPE_DXT5_COL			3	//squish::kDxt5 color, no multiply

int powerof2(int orig)
{
	int result = 1;
	while(result < orig)
		result <<= 1;
	return result;
}

FIBITMAP* imageFromPixels(uint8_t* imgData, uint32_t width, uint32_t height)
{
	//return FreeImage_ConvertFromRawBits(imgData, width, height, width*4, 32, 0xFF0000, 0x00FF00, 0x0000FF, true);	//Doesn't seem to work
	FIBITMAP* curImg = FreeImage_Allocate(width, height, 32);
	FREE_IMAGE_TYPE image_type = FreeImage_GetImageType(curImg);
	if(image_type == FIT_BITMAP)
	{
		int curPos = 0;
		unsigned pitch = FreeImage_GetPitch(curImg);
		BYTE* bits = (BYTE*)FreeImage_GetBits(curImg);
		bits += pitch * height - pitch;
		for(int y = height-1; y >= 0; y--)
		{
			BYTE* pixel = (BYTE*)bits;
			for(int x = 0; x < width; x++)
			{
				pixel[FI_RGBA_RED] = imgData[curPos++];
				pixel[FI_RGBA_GREEN] = imgData[curPos++];
				pixel[FI_RGBA_BLUE] = imgData[curPos++];
				pixel[FI_RGBA_ALPHA] = imgData[curPos++];
				pixel += 4;
			}
			bits -= pitch;
		}
	}
	return curImg;
}

FIBITMAP* PieceImage(uint8_t* imgData, list<piece> pieces, Vec2 maxul, Vec2 maxbr, texHeader th)
{
	Vec2 OutputSize;
	Vec2 CenterPos;
	OutputSize.x = -maxul.x + maxbr.x;
	OutputSize.y = maxul.y - maxbr.y;
	CenterPos.x = -maxul.x;
	CenterPos.y = maxul.y;
	OutputSize.x = uint32_t(OutputSize.x);
	OutputSize.y = uint32_t(OutputSize.y);

	FIBITMAP* result = FreeImage_Allocate(OutputSize.x, OutputSize.y, 32);

	//Create image from this set of pixels
	FIBITMAP* curImg = imageFromPixels(imgData, th.width, th.height);

	//Patch image together from pieces
	for(list<piece>::iterator lpi = pieces.begin(); lpi != pieces.end(); lpi++)
	{
		FIBITMAP* imgPiece = FreeImage_Copy(curImg, 
											(int)((lpi->topLeftUV.x) * th.width + 0.5), (int)((lpi->topLeftUV.y) * th.height + 0.5), 
											(int)((lpi->bottomRightUV.x) * th.width + 0.5), (int)((lpi->bottomRightUV.y) * th.height + 0.5));
		
		//Since pasting doesn't allow you to post an image onto a particular position of another, do that by hand
		int curPos = 0;
		int srcW = FreeImage_GetWidth(imgPiece);
		int srcH = FreeImage_GetHeight(imgPiece);
		unsigned pitch = FreeImage_GetPitch(imgPiece);
		unsigned destpitch = FreeImage_GetPitch(result);
		BYTE* bits = (BYTE*)FreeImage_GetBits(imgPiece);
		BYTE* destBits = (BYTE*)FreeImage_GetBits(result);
		Vec2 DestPos = CenterPos;
		DestPos.x += lpi->topLeft.x;
		//DestPos.y -= lpi->topLeft.y;
		DestPos.y = OutputSize.y - srcH;
		DestPos.y -= CenterPos.y;
		DestPos.y += lpi->topLeft.y;
		DestPos.x = (unsigned int)(DestPos.x);
		DestPos.y = ceil(DestPos.y);
		for(int y = 0; y < srcH; y++)
		{
			BYTE* pixel = bits;
			BYTE* destpixel = destBits;
			destpixel += (unsigned)((DestPos.y + y)) * destpitch;
			destpixel += (unsigned)((DestPos.x) * 4);
			for(int x = 0; x < srcW; x++)
			{
				destpixel[FI_RGBA_RED] = pixel[FI_RGBA_RED];
				destpixel[FI_RGBA_GREEN] = pixel[FI_RGBA_GREEN];
				destpixel[FI_RGBA_BLUE] = pixel[FI_RGBA_BLUE];
				//if(pixel[FI_RGBA_ALPHA] != 0)
					destpixel[FI_RGBA_ALPHA] = pixel[FI_RGBA_ALPHA];
				pixel += 4;
				destpixel += 4;
			}
			bits += pitch;
		}
		
		FreeImage_Unload(imgPiece);
	}
	FreeImage_Unload(curImg);
	
	FIBITMAP* cropped = FreeImage_Copy(result, 4, 4, FreeImage_GetWidth(result) - 2, FreeImage_GetHeight(result) - 2);
	FreeImage_Unload(result);
	
	return cropped;
}


int splitImages(const char* cFilename)
{
	vector<uint8_t> vData;
	FILE* fh = fopen( cFilename, "rb" );
	if(fh == NULL)
	{
		cerr << "Unable to open input file " << cFilename << endl;
		return 1;
	}
	fseek( fh, 0, SEEK_END );
	size_t fileSize = ftell( fh );
	fseek( fh, 0, SEEK_SET );
	vData.reserve( fileSize );
	size_t amt = fread( vData.data(), fileSize, 1, fh );
	fclose( fh );
	cout << "Splitting images from file " << cFilename << endl;
	
	//Figure out what we'll be naming the images
	string sName = cFilename;
	//First off, strip off filename extension
	size_t namepos = sName.find(".anb");
	if(namepos != string::npos)
		sName.erase(namepos);
	//Next, strip off any file path before it
	namepos = sName.rfind('/');
	if(namepos == string::npos)
		namepos = sName.rfind('\\');
	if(namepos != string::npos)
		sName.erase(0, namepos+1);
		
	anbHeader ah;
	memcpy(&ah, vData.data(), sizeof(anbHeader));
	cout << "Num images: " << ah.numImages << endl;
	
	/*for(int i = 0; i < 256; i+=4)
	{
		float f = 0;
		uint32_t iVal = 0;
		memcpy(&f, &(vData.data()[i+sizeof(anbHeader)]), sizeof(float));
		memcpy(&iVal, &(vData.data()[i+sizeof(anbHeader)]), sizeof(uint32_t));
		cout << i+sizeof(anbHeader) << ": " << f << ", " << iVal << endl;
	}
	cout << endl;*/
	
	//Parse through, splitting out before each ZLFW header
	int iCurFile = 0;
	uint64_t startPos = 0;
	for(uint64_t i = 0; i < fileSize; i+=4)	//Definitely not the fastest way to do it... but I don't care
	{
		if(memcmp ( &(vData.data()[i]), "ZLFW", 4 ) == 0)	//Found another file
		{
			bool bPieceTogether = g_bPieceTogether;
			//Get texture header
			uint64_t headerPos = i - sizeof(texHeader);
			texHeader th;
			memcpy(&th, &(vData.data()[headerPos]), sizeof(texHeader));
			
			//if(th.type != TEXTURE_TYPE_DXT1_COL_MUL)	//Type of image we don't support; skip
			//{
				cout << "Tex header: type=" << th.type << ", width=" << th.width << ", height=" << th.height << endl;
				//continue;
			//}
				
			//Search for frame header if we're going to be piecing these together
			/*FrameDesc fd;
			if(bPieceTogether)
			{
				if(iCurFile == 0)	//First one tells us where to start searching backwards from
					startPos = i;
				for(int k = startPos - sizeof(texHeader) - sizeof(FrameDesc); k > 0; k -= 4)
				{
					memcpy(&fd, &(vData.data()[k]), sizeof(FrameDesc));
					if(fd.texOffset != headerPos) continue;
					//Sanity check
					if(fd.texDataSize > 6475888) continue;	//TODO: Test to make sure data size matches
					if(fd.texOffset == 0 || fd.pieceOffset == 0) continue;
					if(fd.pieceOffset > fileSize) continue;
					
					//Ok, found our header
					//cout << "Found header " << endl;
					break;
				}
			}*/
			
			//Decompress WFLZ data
			uint32_t* chunk = NULL;
			const uint32_t decompressedSize = wfLZ_GetDecompressedSize( &(vData.data()[i]) );
			uint8_t* dst = ( uint8_t* )malloc( decompressedSize );
			uint32_t offset = 0;
			int count = 0;
			while( uint8_t* compressedBlock = wfLZ_ChunkDecompressLoop( &(vData.data())[i], &chunk ) )
			{		
				wfLZ_Decompress( compressedBlock, dst + offset );
				const uint32_t blockSize = wfLZ_GetDecompressedSize( compressedBlock );
				offset += blockSize;
			}
			
			//Decompress image
			uint8_t* color = NULL;
			uint8_t* mul = NULL;
			bool bUseMul = false;
			if(g_DecompressFlags != -1)
				th.type = g_DecompressFlags;
				
			cout << "Decomp size: " << decompressedSize << ", w*h: " << th.width << "," << th.height << ", w*h*4:" << th.width * th.height * 4 << endl;
			
			if(th.type == TEXTURE_TYPE_DXT1_COL)
			{
				color = (uint8_t*)malloc(decompressedSize * 8);
				squish::DecompressImage(color, th.width, th.height, dst, squish::kDxt1);
			}
			else if(th.type == TEXTURE_TYPE_DXT5_COL)
			{
				color = (uint8_t*)malloc(th.width * th.height * 4);
				squish::DecompressImage(color, th.width, th.height, dst, squish::kDxt5);
			}
			else if(th.type == TEXTURE_TYPE_256_COL)
			{
				//Read in palette
				vector<pixel> palette;
				uint8_t* cur_data_ptr = dst;
				for(uint32_t curPixel = 0; curPixel < PALETTE_SIZE; curPixel++)
				{
					pixel p;
					p.r = *cur_data_ptr++;
					p.g = *cur_data_ptr++;
					p.b = *cur_data_ptr++;
					p.a = *cur_data_ptr++;
					palette.push_back(p);
				}
				
				//Fill in image
				color = (uint8_t*)malloc(th.width * th.height * 4);
				uint8_t* cur_color_ptr = color;
				for(uint32_t curPixel = 0; curPixel < th.width * th.height; curPixel++)
				{
					*cur_color_ptr++ = palette[*cur_data_ptr].b;
					*cur_color_ptr++ = palette[*cur_data_ptr].g;
					*cur_color_ptr++ = palette[*cur_data_ptr].r;
					*cur_color_ptr++ = palette[*cur_data_ptr].a;
					cur_data_ptr++;
				}
			}
			else if(th.type == TEXTURE_TYPE_RAW)
			{
				//Swap BGR/RGB
				color = (uint8_t*)malloc(th.width * th.height * 4);
				uint8_t* cur_color_ptr = color;
				uint8_t* cur_dst_ptr = dst;
				for(uint32_t curPixel = 0; curPixel < th.width * th.height; curPixel++)
				{
					uint8_t d_R = *cur_dst_ptr++;
					uint8_t d_G = d_R;
					uint8_t d_B = d_R;
					uint8_t d_A = 255;
					
					*cur_color_ptr++ = d_B;
					*cur_color_ptr++ = d_G;
					*cur_color_ptr++ = d_R;
					*cur_color_ptr++ = d_A;
				}
			}
			else
			{
				cout << "Decomp size: " << decompressedSize << ", w*h: " << th.width << "," << th.height << endl;
				cout << "Warning: skipping unknown image type " << th.type << endl;
				delete dst;
				continue;
			}
			
			//cout << "Decomp size: " << decompressedSize << ", w*h: " << th.width << "," << th.height << endl;
			
			//Read in pieces
			Vec2 maxul;
			Vec2 maxbr;
			maxul.x = maxul.y = maxbr.x = maxbr.y = 0;
			list<piece> pieces;
			
			/*if(bPieceTogether)
			{
				PiecesDesc pd;
	
				cout << "FD: " << fd.texOffset << ", " << fd.texDataSize << ", " << fd.pieceOffset << endl;

				memcpy(&pd, &(vData.data()[fd.pieceOffset]), sizeof(PiecesDesc));
				
				cout << "PD: " << pd.numPieces << endl;
				
				for(uint32_t j = 0; j < pd.numPieces; j++)
				{
					piece p;
					memcpy(&p, &(vData.data()[fd.pieceOffset+j*sizeof(piece)+sizeof(PiecesDesc)]), sizeof(piece));
					//Store our maximum values, so we know how large the image is
					if(p.topLeft.x < maxul.x)
						maxul.x = p.topLeft.x;
					if(p.topLeft.y > maxul.y)
						maxul.y = p.topLeft.y;
					if(p.bottomRight.x > maxbr.x)
						maxbr.x = p.bottomRight.x;
					if(p.bottomRight.y < maxbr.y)
						maxbr.y = p.bottomRight.y;
					
					//cout << "Piece: " << p.topLeft.x << ", " << p.topLeft.y << ", " << p.topLeftUV.x  << ", " << p.topLeftUV.y << ", " << p.bottomRight.x << ", " << p.bottomRight.y << ", " << p.bottomRightUV.x << ", " << p.bottomRightUV.y << endl;
					
					//Sanity check: Skip over piecing if there's only one piece total that fills the whole thing
					if(pd.numPieces == 1 && p.topLeftUV.x == 0.0 && p.topLeftUV.y == 0.0 && p.bottomRightUV.x == 1.0 && p.bottomRightUV.y == 1.0) bPieceTogether = false;
					
					pieces.push_back(p);
				}
			}*/
			
			uint8_t* dest_final = color;
			
			FIBITMAP* result = NULL;
			if(bPieceTogether && pieces.size())
				result = PieceImage(dest_final, pieces, maxul, maxbr, th);
			else
				result = imageFromPixels(dest_final, th.width, th.height);
			
			ostringstream oss;
			oss << "output/" << sName << '_' << iCurFile+1 << ".png";
			cout << "Saving " << oss.str() << endl;
			FreeImage_Save(FIF_PNG, result, oss.str().c_str());
			FreeImage_Unload(result);
			
			//Free allocated memory
			if(dst != dest_final)
				free(dst);
			free(dest_final);
			//free(color);
			//free(mul);
			
			iCurFile++;
		}
	}
	return 0;
}

int main(int argc, char** argv)
{
	g_DecompressFlags = -1;
	g_bSeparate = false;
	g_bPieceTogether = false;
	g_bColOnly = g_bMulOnly = false;
	FreeImage_Initialise();
#ifdef _WIN32
	CreateDirectory(TEXT("output"), NULL);
#else
	int result = system("mkdir -p output");
#endif
	list<string> sFilenames;
	//Parse commandline
	for(int i = 1; i < argc; i++)
	{
		string s = argv[i];
		if(s == "-0")
			g_DecompressFlags = 0;
		else if(s == "-1")
			g_DecompressFlags = 1;
		else if(s == "-2")
			g_DecompressFlags = 2;
		else if(s == "-3")
			g_DecompressFlags = 3;
		else if(s == "-4")
			g_DecompressFlags = 4;
		else if(s == "-5")
			g_DecompressFlags = 5;
		else if(s == "-6")
			g_DecompressFlags = 6;
		else if(s == "-separate")
			g_bSeparate = true;
		else if(s == "-col-only")
		{
			g_bColOnly = true;
			g_bMulOnly = false;
			g_bSeparate = true;
		}
		else if(s == "-mul-only")
		{
			g_bMulOnly = true;
			g_bColOnly = false;
			g_bSeparate = true;
		}
		else if(s == "-nopiece")
			g_bPieceTogether = false;
		else if(s == "-piece")
			g_bPieceTogether = true;
		else
			sFilenames.push_back(s);
	}
	//Decompress ANB files
	for(list<string>::iterator i = sFilenames.begin(); i != sFilenames.end(); i++)
		splitImages((*i).c_str());
	FreeImage_DeInitialise();
	return 0;
}
