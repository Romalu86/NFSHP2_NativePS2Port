#include "FlailerChunks.hpp"
#include "FlailerDB.hpp"
#include "indep/src/MoviePlayer/MV3Codec.h"
#include "Texture.hpp"
#include "Language.hpp"
#include "bware/inc/bWare.hpp"
#include "indep/inc/Ecstasy.hpp"
#include "indep/inc/eMath.hpp"
#include "bware/inc/bMath.hpp"
#include "bware/inc/bChunk.hpp"
#include "bware/inc/bWare.hpp"
#include "bware/inc/bFile.hpp"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace hp2::frontend {

// PAL global constructor1464D8 initializes the clip window to644x480.
// The writable raster bytes33019C..3301A1 and help mapping2F09C0..DD
// remain one live state, rather than constants copied into each text object.
FlailerTextRasterState flailerTextRasterState{};
// The actual145768/1457D0 writers update2F09A0+14/+18: live strips and
// signed high-water count. All texture batches participate in the same cells.
FlailerRuntimeStatistics flailerRuntimeStatistics{};

void SetFlailerTextClipWindow(float left,float top,float right,float bottom)
{
    flailerTextRasterState.minimum={left,top};
    flailerTextRasterState.maximum={right,bottom};
}

void SetFlailerTextClipWindow(const float minimum[2],const float maximum[2])
{
    flailerTextRasterState.minimum[0]=minimum[0];
    flailerTextRasterState.minimum[1]=minimum[1];
    flailerTextRasterState.maximum[0]=maximum[0];
    flailerTextRasterState.maximum[1]=maximum[1];
}

void ResetFlailerTextClipWindow()
{
    flailerTextRasterState.maximum={644.0f,480.0f};
    flailerTextRasterState.minimum={0.0f,0.0f};
}

std::uint32_t next_power_of_2(std::uint32_t value)
{
    if (!value) return 0;
    value=(value-1u)>>1;
    std::uint32_t result=2;
    while (value) { result<<=1;value>>=1; }
    return result;
}

float Font::GetKerning(int nextCharacter,const tTTGlyph* glyph) const
{
    const auto next=static_cast<std::uint8_t>(nextCharacter);
    for (std::int32_t index=0;index<glyph->kerningCount;++index) {
        const auto& pair=kernings[glyph->firstKerning+static_cast<std::uint32_t>(index)];
        if (pair.nextCharacter==next) return pair.adjustment;
    }
    return 0.0f;
}

float Font::GetWidestCharacter(const char* text) const
{
    float widest=0.0f;
    for (const auto* cursor=reinterpret_cast<const unsigned char*>(text);*cursor;++cursor) {
        const tTTGlyph* glyph=characters[*cursor];
        if (glyph && widest<glyph->advance) widest=glyph->advance;
    }
    return widest;
}

void FlailerObjectChunk::SetScaleX(float value)
{
    if (scaleX!=value) { scaleX=value;updateKind=3; }
}

void FlailerObjectChunk::SetScaleY(float value)
{
    if (scaleY!=value) { scaleY=value;updateKind=3; }
}

void FlailerObjectChunk::SetTopLeftX(float value)
{
    if (topLeftX!=value) { topLeftX=value;updateKind=4; }
}

void FlailerObjectChunk::SetTopLeftY(float value)
{
    if (topLeftY!=value) { topLeftY=value;updateKind=4; }
}

// Complete original SetClipToPoly140A70..140AA8. The source clip word is
// shared by all object kinds; only text also enables the clipping format bit.
void FlailerObjectChunk::SetClipToPoly(std::uint32_t value)
{
    if(clipObjectKey==value)return;
    updateKind=4;clipObjectKey=value;
    if(kind==1)static_cast<FlailerTextChunk*>(this)->SetFormatOptions(0x10);
}

void FlailerTextChunk::SetFormatOptions(std::int32_t options)
{
    updateKind=5;
    formatOptions|=static_cast<std::uint32_t>(options);
}

void FlailerTextChunk::ClearFormatOptions(std::int32_t options)
{
    updateKind=5;
    formatOptions&=~static_cast<std::uint32_t>(options);
}

void FlailerTextChunk::SetColor(std::uint32_t rgb)
{
    const std::uint32_t replacement=(color&0xff000000u)|(rgb&0x00ffffffu);
    if (color!=replacement) { color=replacement;updateKind=7; }
}

void FlailerTextChunk::UpdateLanguageText()
{
    updateKind=5;
}

void FlailerScreenResource::UpdateLanguageText()
{
    for (auto* object=objects.next;object!=&objects;object=object->next)
        if (object->kind==1)static_cast<FlailerTextChunk*>(object)->UpdateLanguageText();
}

// Complete PAL140300..140374 DeleteAllFlailerStrips. A cached bitmap
// descriptor is borrowed from a texture pack, so deleting the strip must
// also clear that binding and mark the bitmap for the next loaded lookup.
void FlailerScreenResource::DeleteAllFlailerStrips()
{
    for (auto* object=objects.next;object!=&objects;object=object->next) {
        object->DeleteFlailerStrip();
        if (object->kind==2) {
            auto* bitmap=static_cast<FlailerBitmapChunk*>(object);
            bitmap->texture=nullptr;bitmap->textureStorage.reset();bitmap->updateKind=10;
        }
    }
}

void FlailerUpdateLanguageText(FlailerResources& resources)
{
    auto& root=resources.PackageRoot();
    for (auto* package=root.next;package!=&root;package=package->next)
        for (auto* screen=package->screenRoot.next;screen!=&package->screenRoot;screen=screen->next)
            screen->UpdateLanguageText();
    // PAL1416B4 shares the same global strip/binding retirement tail.
    resources.DeleteAllStrips();
}

void FlailerTextChunk::SetAlpha(std::int32_t alpha)
{
    const std::uint32_t replacement=(color&0x00ffffffu)|(static_cast<std::uint32_t>(alpha)<<24);
    if (color!=replacement) { color=replacement;updateKind=6; }
}

std::int32_t FlailerTextChunk::GetAlpha() const
{
    return static_cast<std::int32_t>(color>>24);
}

void FlailerBitmapChunk::SetAlpha(std::int32_t alpha,std::int32_t corner)
{
    updateKind=6;
    const std::uint32_t value=static_cast<std::uint32_t>(alpha)<<24;
    if (corner==-1) {
        for (auto& color:colors) color=(color&0x00ffffffu)|value;
    } else {
        auto& color=colors[static_cast<std::size_t>(corner)];
        color=(color&0x00ffffffu)|value;
    }
}

void FlailerBitmapChunk::SetRotation(float value)
{
    const std::int32_t replacement=convertFloatToInt32(value);
    if (rotation!=replacement) { rotation=replacement;updateKind=9; }
}

void FlailerBitmapChunk::SetColor(std::uint32_t value)
{
    if (colors[0]!=value) updateKind=7;
    colors[3]=value;colors[0]=value;colors[1]=value;colors[2]=value;
}

void FlailerBitmapChunk::ChangeTexture(const EditorMarkerTexture* value)
{
    const std::uint32_t textureHash=value->key;
    updateKind=10;textureKey=textureHash;texture=value;
    textureStorage=textureMetadataOwner(value);
}

void FlailerBitmapChunk::ChangeTexture(std::uint32_t textureHash)
{
    textureKey=textureHash;updateKind=10;texture=nullptr;
    textureStorage.reset();
}

std::int32_t FlailerBitmapChunk::GetAlpha(std::int32_t corner) const
{
    return static_cast<std::int32_t>(colors[static_cast<std::size_t>(corner)]>>24);
}

bool MaybeClipPoly(FlailerPoly* polygon,const float minimum[2],const float maximum[2])
{
    auto& vertices=polygon->vertices;
    const float oldRight=vertices[1].position[0];
    const float oldLeft=vertices[0].position[0];
    const float oldBottom=vertices[2].position[1];
    const float oldTop=vertices[0].position[1];
    if (oldRight<minimum[0] || maximum[0]<=oldLeft ||
        oldBottom<minimum[1] || maximum[1]<=oldTop) return true;
    if (oldLeft<minimum[0]) {
        const float amount=std::fabs((minimum[0]-oldLeft)/(oldRight-oldLeft));
        const float complement=1.0f-amount;
        const float right=amount*oldRight;
        const float newU=complement*vertices[0].textureCoordinate[0]+amount*vertices[1].textureCoordinate[0];
        const float newX=complement*oldLeft+right;
        vertices[0].textureCoordinate[0]=newU;
        vertices[0].position[0]=newX;vertices[3].position[0]=newX;
        vertices[3].textureCoordinate[0]=newU;
    }
    if (maximum[0]<=oldRight) {
        const float left=vertices[0].position[0];
        const float amount=std::fabs((maximum[0]-oldRight)/(oldRight-left));
        const float complement=1.0f-amount;
        const float leftContribution=amount*left;
        const float newU=amount*vertices[0].textureCoordinate[0]+complement*vertices[1].textureCoordinate[0];
        const float newX=leftContribution+complement*oldRight;
        vertices[1].textureCoordinate[0]=newU;
        vertices[1].position[0]=newX;vertices[2].position[0]=newX;
        vertices[2].textureCoordinate[0]=newU;
    }
    if (oldTop<minimum[1]) {
        const float amount=std::fabs((minimum[1]-oldTop)/(oldBottom-oldTop));
        const float complement=1.0f-amount;
        const float bottom=amount*oldBottom;
        const float newV=complement*vertices[0].textureCoordinate[1]+amount*vertices[2].textureCoordinate[1];
        const float newY=complement*oldTop+bottom;
        vertices[0].textureCoordinate[1]=newV;
        vertices[0].position[1]=newY;vertices[1].position[1]=newY;
        vertices[1].textureCoordinate[1]=newV;
    }
    if (maximum[1]<=oldBottom) {
        const float top=vertices[0].position[1];
        const float amount=std::fabs((maximum[1]-oldBottom)/(oldBottom-top));
        const float complement=1.0f-amount;
        const float topContribution=amount*top;
        const float newV=amount*vertices[0].textureCoordinate[1]+complement*vertices[2].textureCoordinate[1];
        const float newY=topContribution+complement*oldBottom;
        vertices[2].textureCoordinate[1]=newV;
        vertices[2].position[1]=newY;vertices[3].position[1]=newY;
        vertices[3].textureCoordinate[1]=newV;
    }
    return false;
}

float FlailerTextDistanceToNextChar(const char* text,char delimiter,const Font* font)
{
    float distance=0.0f;
    if (!text) return distance;
    for (const auto* cursor=reinterpret_cast<const unsigned char*>(text);
         *cursor && *cursor!=static_cast<unsigned char>(delimiter);++cursor) {
        if (const tTTGlyph* glyph=font->characters[*cursor])
            distance+=static_cast<float>(convertFloatToInt32(glyph->advance));
    }
    return distance;
}

bool IsHelpCharacter(std::uint8_t value)
{
    return static_cast<std::uint8_t>(value+0x80u)<0x1eu && value!=0x92u;
}

FlailerObjectChunk* FindFlailerObject(FlailerObjectChunk* root,std::uint32_t key)
{
    for (auto* object=root->previous;object!=root;object=object->previous)
        if (object->key==key) return object;
    return nullptr;
}

std::uint32_t GetEPolyChecksum(const FlailerPoly* polygon)
{
    std::uint32_t sum=0;
    for (const auto& vertex:polygon->vertices) {
        for (float value:vertex.position) { std::uint32_t bits;std::memcpy(&bits,&value,4);sum+=bits; }
        for (float value:vertex.textureCoordinate) { std::uint32_t bits;std::memcpy(&bits,&value,4);sum+=bits; }
        sum+=vertex.color;
    }
    for (std::uint32_t value:polygon->rasterState) sum+=value;
    return sum;
}

bool FlailerRenderManager::IsScreenVisible(std::uint32_t key) const
{
    for (const auto* screen:visibleScreens)
        if (screen && screen->key==key) return true;
    return false;
}

struct FlailerResources::Storage {
    platform::NativePromptDevice promptDevice{platform::NativePromptDevice::Original};
    struct FontStorage {
        Font font{};
        std::vector<tTTGlyph> glyphs;
        std::vector<FontKerning> kernings;
    };
    struct Bundle {
        std::uint64_t token{};
        std::size_t offset{},size{};
        std::vector<FontStorage*> fonts;
        std::vector<FlailerPackageResource*> packages;
        std::vector<FlailerScreenResource*> screens;
        std::vector<FlailerTextChunk*> text;
        std::vector<FlailerBitmapChunk*> bitmaps;
        std::vector<TrackRouteEditorResources*> models;
    };
    std::vector<Bundle> bundles;
    std::uint64_t nextBundle{1};
    std::vector<std::uint8_t> authored;
    std::vector<std::unique_ptr<FontStorage>> fonts;
    FlailerPackageResource packageRoot{};
    std::vector<std::unique_ptr<FlailerPackageResource>> packages;
    std::vector<std::unique_ptr<FlailerScreenResource>> screens;
    std::vector<std::unique_ptr<FlailerTextChunk>> text;
    std::vector<std::unique_ptr<FlailerBitmapChunk>> bitmaps;
    FlailerStripPool stripPool;
    EditorMarkerResources ownedModelRegistry{};
    EditorMarkerResources* modelRegistry{};
    std::vector<std::unique_ptr<TrackRouteEditorResources>> modelAssets;
    const EditorMarkerTexture* textures{};
    std::size_t textureCount{};
    mutable bool missingFontReported{};
};

namespace {
std::vector<std::vector<std::uint8_t>> readFrontendBundles(GameResources& resources)
{
    std::vector<std::vector<std::uint8_t>> bundles;
    bundles.push_back(resources.Load("GLOBAL/GLOBALB.LZC"));
    bundles.push_back(resources.Load("FRONTEND/FRONTB.LZC"));
    return bundles;
}

std::vector<std::vector<std::uint8_t>> oneFrontendBundle(std::vector<std::uint8_t> authored)
{
    std::vector<std::vector<std::uint8_t>> bundles;
    bundles.push_back(std::move(authored));return bundles;
}

float authoredFloat(const std::uint8_t* bytes)
{
    const std::uint32_t bits=ReadU32(bytes);
    float value;
    std::memcpy(&value,&bits,sizeof(value));
    Require(std::isfinite(value),"Flailer resource contains a non-finite metric");
    return value;
}

void decodeObject(FlailerObjectChunk& object,const std::uint8_t* bytes)
{
    object.key=ReadU32(bytes+8);object.parentKey=ReadU32(bytes+12);
    std::memcpy(object.name.data(),bytes+16,object.name.size());
    object.kind=bytes[40];object.visible=bytes[41];
    object.authoredFlags=bytes[42];object.updateKind=bytes[43];
    object.topLeftX=authoredFloat(bytes+48);object.topLeftY=authoredFloat(bytes+52);
    object.scaleX=authoredFloat(bytes+56);object.scaleY=authoredFloat(bytes+60);
    const std::uint32_t rotation=ReadU32(bytes+64);
    std::memcpy(&object.rotation,&rotation,sizeof(rotation));
    object.authoredMetrics={ReadU32(bytes+68),ReadU32(bytes+72)};
}

EcstasyUiPlacement raceHudPlacement(std::uint32_t screen,const FlailerObjectChunk& object)
{
    using Placement=EcstasyUiPlacement;
    static const auto chyron=hashString("CHYRON");
    if (screen==chyron) return Placement::HudLeftDuringRace;

    // The three authored bust camera variants share these overlay widgets.
    if (screen==0x3EA89B7Cu || screen==0xD4D25EE7u || screen==0xC17316C9u) {
        switch (object.key) {
        case 0x001450DDu: return Placement::HudLeft;        // DATE
        case 0x0000D999u:                                   // REC
        case 0x61CB41DFu:                                   // REC_DOT
        case 0x001D380Eu: return Placement::HudRight;       // TIME
        case 0x76494C5Bu:                                   // BUSTED_DISPLAY
        case 0x233DFBE1u: return Placement::CenteredCanvas; // BUSTED_SCORE
        default: return Placement::OriginalCanvas;
        }
    }
    if (screen!=0xF1F1F8E5u && screen!=0xA8EB7AF0u && screen!=0x6283BE92u)
        return Placement::OriginalCanvas;

    // Split HUD resources already encode the top and bottom player positions
    // in the shared 640x480 design space.
    switch (object.parentKey) {
    case 0x427BE6DDu: // Performance and position readout
    case 0xC0E6D1B6u: // Live standings rows
    case 0xC0E6D1B7u:
    case 0xC0E6D1B8u:
    case 0xC0E6D1B9u:
        return Placement::HudLeft;
    case 0x068E2C75u: // Be the Cop
    case 0x0B603727u: // Pursuit indicators
    case 0xA19AE030u:
    case 0xED9FD777u:
    case 0xB7A3BC7Au: // Lap and timing panel
    case 0xC258F297u: // Tracker
    case 0x0FAF5A2Fu: // Gear
    case 0xCD086C26u: // Digital gauges
    case 0x5C9C03A7u: // Score animation travels to the right-side total
        return Placement::HudRight;
    case 0x062743F5u: // Radar
    case 0x8E943093u: // Reticle
    case 0xBBBB857Cu: // Wrong way
    case 0xC707E093u: // Race over text
    case 0xE1CB0B82u: // Tire force
    case 0xF74C9D43u: // Reset message
        return Placement::CenteredCanvas;
    case 0xCD086C25u: // Analogue gauges share a group across two camera styles
        switch (object.key) {
        case 0xD2F7FA46u: // SPEED_STRING
        case 0x70DFB037u: // SPEED_STRING_2
        case 0xCF9BF6F3u: // SHIFT_UP2
            return Placement::HudRight;
        default:
            return Placement::CenteredCanvas;
        }
    case 0x457755C9u: // Race position, split messages and gauge/pursuit art
        switch (object.key) {
        case 0x3405C44Eu: // RACE_POSITION
        case 0x004E27A0u: // BIGRACE_POSITION
        case 0x9A492668u: // BIGRACE_EXTENSION
        case 0x84BB420Fu: // BIGRACE_TOTAL
        case 0x4C769C11u: // BIGRACE_POSITION_2
        case 0x363392AEu: // Live standings positioning
        case 0x363392AFu:
        case 0xA3E3B399u:
        case 0x6EB9985Du: // Dividers beside the standings
            return Placement::HudLeft;
        case 0x89C3AC59u: // RUNNING_SPLIT_TIME
        case 0xD1E5C713u: // COMPARISON_SPLIT_TIME
            return Placement::CenteredCanvas;
        default:
            return Placement::HudRight;
        }
    default:
        return Placement::OriginalCanvas;
    }
}

void bindRaceHudRows(FlailerScreenResource& screen)
{
    if (screen.key!=0xF1F1F8E5u && screen.key!=0xA8EB7AF0u && screen.key!=0x6283BE92u)
        return;

    // Each live standings row owns a number swatch and a status backing.
    // Timing uses the same backing for the label and value in each row.
    constexpr std::pair<std::uint32_t,std::uint32_t> timingRows[]={
        {0x0390D3DFu,0x40E81BE1u}, {0x6486623Bu,0x40E81BE1u},
        {0x0390D3E0u,0x40E81BE2u}, {0xA8247197u,0x40E81BE2u},
        {0xEB406FECu,0x40E81BE2u}, {0xF7292837u,0x40E81BE2u},
        {0x0390D3E1u,0x40E81BE3u}, {0xFC1B498Au,0x40E81BE3u}
    };
    for (auto* object=screen.objects.next;object!=&screen.objects;object=object->next) {
        if (object->kind!=1) continue;
        std::uint32_t rowKey{};
        for (const auto& [textKey,backing]:timingRows)
            if (object->key==textKey) rowKey=backing;
        for (std::uint32_t row=0;row<4;++row) {
            if (object->key==0x434907BCu+row) rowKey=0x674D5834u+row; // Position
            if (object->key==0x604E2C4Eu+row) rowKey=0x9D308573u+row; // Status
        }
        if (!rowKey) continue;
        for (auto* row=screen.objects.next;row!=&screen.objects;row=row->next) {
            if (row->key==rowKey && row->kind==2) {
                static_cast<FlailerTextChunk*>(object)->hudRow=static_cast<FlailerBitmapChunk*>(row);
                break;
            }
        }
    }
}

void appendObject(FlailerScreenResource& screen,FlailerObjectChunk& object)
{
    object.uiPlacement=raceHudPlacement(screen.key,object);
    if (object.kind==2 && object.uiPlacement==EcstasyUiPlacement::OriginalCanvas) {
        const auto& bitmap=static_cast<const FlailerBitmapChunk&>(object);
        if (object.key==0x00163ce5u || object.key==0x631d1512u || object.key==0x2962c18fu ||
            (object.key==0x67d295cau && object.authoredFlags>=6) || bitmap.textureKey==0x9067abf4u) {
            // The grid, blueprint and their shadows share a continuous backdrop.
            object.uiPlacement=EcstasyUiPlacement::CoverCanvas;
        }
    }
    if (screen.key==0xF1F1F8E5u && object.kind==2 && object.parentKey==0x062743F5u) {
        // The single-player radar and the rear-view mirror use the same
        // centered canvas. The authored backing spans x=220..414 and the
        // ten lit segments span x=232..400. Align both to the mirror at x=320.
        if (object.key==0x6788C391u)object.topLeftX+=3.0f; // RADAR_BG
        else {
            object.topLeftX+=4.0f;
            object.topLeftY+=0.5f;
        }
    }
    if (object.kind==1 && object.parentKey==0x457755C9u &&
        (object.key==0x89C3AC59u || object.key==0xD1E5C713u) &&
        object.uiPlacement==EcstasyUiPlacement::CenteredCanvas) {
        // Retain the spacing between the two checkpoint lines. On solo
        // and lower split HUDs the first line started inside the radar.
        object.topLeftX=screen.key==0xF1F1F8E5u ? 320.0f:319.0f;
        if(screen.key==0xF1F1F8E5u)object.topLeftY+=7.0f;
        if(screen.key==0x6283BE92u)object.topLeftY+=15.0f;
    }
    auto* previous=screen.objects.previous;
    previous->next=&object;screen.objects.previous=&object;
    object.previous=previous;object.next=&screen.objects;
}
} // namespace

FlailerResources::FlailerResources(EditorMarkerResources& modelRegistry)
    : FlailerResources(std::vector<std::vector<std::uint8_t>>{},nullptr,0,&modelRegistry)
{
}

FlailerResources::FlailerResources(GameResources& resources,
    const EditorMarkerTexture* textures,std::size_t textureCount,EditorMarkerResources* modelRegistry)
    : FlailerResources(readFrontendBundles(resources),textures,textureCount,modelRegistry)
{
}

FlailerResources::FlailerResources(std::vector<std::uint8_t> authored,
    const EditorMarkerTexture* textures,std::size_t textureCount,EditorMarkerResources* modelRegistry)
    : FlailerResources(oneFrontendBundle(std::move(authored)),textures,textureCount,modelRegistry)
{
}

FlailerResources::FlailerResources(std::vector<std::vector<std::uint8_t>> bundles,
    const EditorMarkerTexture* textures,std::size_t textureCount,EditorMarkerResources* modelRegistry)
    : storage_(std::make_unique<Storage>())
{
    Require(textures || !textureCount,"Flailer texture range has no owner");
    storage_->modelRegistry=modelRegistry ? modelRegistry : &storage_->ownedModelRegistry;
    for (const auto& bundle:bundles) {
        Require(bundle.size()<=storage_->authored.max_size()-storage_->authored.size(),
            "Frontend bundle set exceeds native capacity");
        storage_->authored.insert(storage_->authored.end(),bundle.begin(),bundle.end());
        // The resource loader aligns each file independently. A concatenated
        // GLOBALB/FRONTB byte view is valid for the unaligned Flailer chunks,
        // but would change the origin of every16-aligned eSolid/texture.
        // Decode each retained file once into the same live model catalogue.
        storage_->modelAssets.push_back(std::make_unique<TrackRouteEditorResources>(bundle,
            storage_->modelRegistry));
    }
    storage_->packageRoot.next=storage_->packageRoot.previous=&storage_->packageRoot;
    const auto& bytes=storage_->authored;
    storage_->textures=textures;storage_->textureCount=textureCount;
    const auto visit=[&](const auto& self,std::size_t start,std::size_t size,unsigned depth,
                         FlailerPackageResource* package)->void {
        Require(depth<=32,"Flailer chunk nesting exceeds 32 levels");
        for (const auto& chunk:ReadChunks(bytes,start,size)) {
            if (chunk.id==0x80010200u) {
                auto created=std::make_unique<FlailerPackageResource>();
                created->screenRoot.next=created->screenRoot.previous=&created->screenRoot;
                created->previous=storage_->packageRoot.previous;created->next=&storage_->packageRoot;
                created->previous->next=created.get();storage_->packageRoot.previous=created.get();
                auto* current=created.get();storage_->packages.push_back(std::move(created));
                self(self,chunk.payloadOffset,chunk.size,depth+1,current);
            } else if (chunk.id==0x15010u) {
                const auto offset=chunk.headerOffset;
                const auto length=chunk.size+8u;
                Require(length>=28,"Flailer font header is truncated");
                const auto* record=bytes.data()+offset;
                const auto glyphCount=ReadU32(record+12),kerningCount=ReadU32(record+16);
                const auto glyphOffset=ReadU32(record+20),kerningOffset=ReadU32(record+24);
                Require(glyphCount<=256 && kerningCount<=1048576,"Flailer font count exceeds its authored domain");
                Require(Inside(glyphOffset,static_cast<std::uint64_t>(glyphCount)*32u,length) &&
                        (!kerningCount || Inside(kerningOffset,static_cast<std::uint64_t>(kerningCount)*8u,length)),
                        "Flailer font arrays exceed their chunk");
                auto font=std::make_unique<Storage::FontStorage>();
                font->font.key=ReadU32(record+8);
                font->glyphs.resize(glyphCount);font->kernings.resize(kerningCount);
                for (std::size_t index=0;index<kerningCount;++index) {
                    const auto* pair=record+kerningOffset+index*8;
                    font->kernings[index]={pair[0],authoredFloat(pair+4)};
                }
                for (std::size_t index=0;index<glyphCount;++index) {
                    const auto* raw=record+glyphOffset+index*32;
                    auto& glyph=font->glyphs[index];glyph.character=raw[0];
                    const std::uint16_t count=ReadU16(raw+2);
                    std::memcpy(&glyph.kerningCount,&count,sizeof(count));
                    glyph.firstKerning=ReadU32(raw+28);glyph.advance=authoredFloat(raw+20);
                    glyph.atlasPosition={authoredFloat(raw+4),authoredFloat(raw+8)};
                    glyph.size={authoredFloat(raw+12),authoredFloat(raw+16)};
                    const auto height=authoredFloat(raw+16);
                    if (font->font.lineHeight<height) font->font.lineHeight=height;
                    Require(glyph.kerningCount<=0 || Inside(glyph.firstKerning,
                        static_cast<std::uint16_t>(glyph.kerningCount),kerningCount),
                        "Flailer glyph kerning range exceeds its font");
                    font->font.characters[glyph.character]=&glyph;
                }
                font->font.kernings=font->kernings.data();font->font.kerningCount=kerningCount;
                font->font.fixedAdvance=font->font.GetWidestCharacter("1234567890:- ");
                storage_->fonts.push_back(std::move(font));
            } else if (chunk.id==0x80010201u) {
                Require(package,"Flailer screen has no package owner");
                FlailerScreenResource* screen=nullptr;
                for (const auto& child:ReadChunks(bytes,chunk.payloadOffset,chunk.size)) {
                    const auto* record=bytes.data()+child.payloadOffset;
                    if (child.id==0x10203u) {
                        Require(child.size>=40 && !screen,"Flailer screen header is absent or repeated");
                        auto created=std::make_unique<FlailerScreenResource>();
                        created->key=ReadU32(record+8);
                        created->sharedScreenKey=ReadU32(record+20);
                        const auto visibility=ReadU16(record+30);
                        std::memcpy(&created->visibility,&visibility,sizeof(visibility));
                        created->objects.next=created->objects.previous=&created->objects;
                        created->next=&package->screenRoot;created->previous=package->screenRoot.previous;
                        created->previous->next=created.get();package->screenRoot.previous=created.get();
                        screen=created.get();storage_->screens.push_back(std::move(created));
                    } else if (child.id==0x10212u) {
                        Require(screen && child.size>=100,"Flailer text precedes its screen or is truncated");
                        auto text=std::make_unique<FlailerTextChunk>();decodeObject(*text,record);
                        Require(text->kind==1,"Flailer text chunk has a different object kind");
                        text->clipObjectKey=ReadU32(record+76);text->fontKey=ReadU32(record+80);
                        text->textCapacity=ReadU32(record+84);text->formatOptions=ReadU32(record+88);
                        text->color=ReadU32(record+92);
                        text->languageId=ReadU32(record+96);
                        const auto stored=std::min<std::size_t>(text->textCapacity,child.size-100);
                        Require(stored,"Flailer text has no inline buffer");
                        const auto* value=reinterpret_cast<const char*>(record+100);
                        const auto* end=static_cast<const char*>(std::memchr(value,0,stored));
                        // Some localized bundles store only the initial value.
                        // The capacity field can still describe the editor's
                        // buffer. Accept short storage only with a terminator
                        // inside this chunk; localized text can grow on use.
                        Require(stored==text->textCapacity || end,"Flailer text buffer exceeds its chunk");
                        text->textCapacity=static_cast<std::uint32_t>(stored);
                        // Several shipped fixed-width values (for example
                        // four-byte "help") occupy the entire authored span.
                        // Give their exact counted bytes a host terminator;
                        // never read the following chunk as part of a string.
                        const auto length=end?static_cast<std::size_t>(end-value):text->textCapacity;
                        text->ownedText.resize(std::max<std::size_t>(static_cast<std::size_t>(text->textCapacity)+1u,5u),0);
                        std::memcpy(text->ownedText.data(),value,length);
                        text->text=text->ownedText.data();
                        appendObject(*screen,*text);++screen->textCount;
                        storage_->text.push_back(std::move(text));
                    } else if (child.id==0x10210u) {
                        Require(screen && child.size>=144,"Flailer bitmap precedes its screen or is truncated");
                        auto bitmap=std::make_unique<FlailerBitmapChunk>();decodeObject(*bitmap,record);
                        Require(bitmap->kind==2,"Flailer bitmap chunk has a different object kind");
                        bitmap->clipObjectKey=ReadU32(record+76);
                        bitmap->textureKey=ReadU32(record+80);
                        for (std::size_t corner=0;corner<4;++corner)
                            for (std::size_t axis=0;axis<2;++axis)
                                bitmap->textureCoordinates[corner][axis]=authoredFloat(record+112+corner*8+axis*4);
                        for (std::size_t corner=0;corner<4;++corner) bitmap->colors[corner]=ReadU32(record+88+corner*4);
                        bitmap->texture=FindTexture(bitmap->textureKey);
                        appendObject(*screen,*bitmap);++screen->bitmapCount;
                        storage_->bitmaps.push_back(std::move(bitmap));
                    }
                }
                Require(screen,"Flailer screen container has no screen header");
                bindRaceHudRows(*screen);
            } else if (chunk.IsContainer()) self(self,chunk.payloadOffset,chunk.size,depth+1,package);
        }
    };
    visit(visit,0,bytes.size(),0,nullptr);
}

FlailerResources::~FlailerResources()=default;

std::uint64_t FlailerResources::AppendBundle(const std::vector<std::uint8_t>& authored)
{
    auto& root=*storage_;
    Require(root.nextBundle!=0,"Flailer bundle identity exhausted");
    // Parse once with its original file alignment. Until this succeeds the
    // package and font lists in the live root are untouched. Its eSolid/TPK
    // owners already use the same model registry and retire on any exception.
    FlailerResources decoded(oneFrontendBundle(authored),root.textures,root.textureCount,root.modelRegistry);
    auto& added=*decoded.storage_;
    // The reference centers the authored pause panel and puts its logo
    // above the panel. The full-screen dimmer remains anchored at the edge.
    if(auto* pause=decoded.FindScreen(0x605d2bf1u)) {
        for(auto* object=pause->objects.next;object!=&pause->objects;object=object->next) {
            if(object->kind==2 && static_cast<const FlailerBitmapChunk*>(object)->textureKey==0xc9f75a5bu)
                object->SetTopLeftX((640.0f-296.0f*object->scaleX)*0.5f);
            else if(object->topLeftX>=100.0f)object->SetTopLeftX(object->topLeftX-90.0f);
        }
    }
    Storage::Bundle block;
    block.token=root.nextBundle;block.offset=root.authored.size();block.size=authored.size();
    const auto prepare=[](auto& destination,auto& members,const auto& incoming) {
        destination.reserve(destination.size()+incoming.size());
        members.reserve(incoming.size());
        for (const auto& value:incoming)members.push_back(value.get());
    };
    prepare(root.fonts,block.fonts,added.fonts);
    prepare(root.packages,block.packages,added.packages);
    prepare(root.screens,block.screens,added.screens);
    prepare(root.text,block.text,added.text);
    prepare(root.bitmaps,block.bitmaps,added.bitmaps);
    prepare(root.modelAssets,block.models,added.modelAssets);
    Require(authored.size()<=root.authored.max_size()-root.authored.size(),"Flailer bundle exceeds native capacity");
    root.authored.reserve(root.authored.size()+authored.size());
    root.bundles.reserve(root.bundles.size()+1);
    root.authored.insert(root.authored.end(),authored.begin(),authored.end());
    if (added.packageRoot.next!=&added.packageRoot) {
        auto* first=added.packageRoot.next;
        auto* last=added.packageRoot.previous;
        root.packageRoot.previous->next=first;first->previous=root.packageRoot.previous;
        last->next=&root.packageRoot;root.packageRoot.previous=last;
        added.packageRoot.next=added.packageRoot.previous=&added.packageRoot;
    }
    const auto transfer=[](auto& destination,auto& incoming) {
        for (auto& value:incoming)destination.push_back(std::move(value));
        incoming.clear();
    };
    transfer(root.fonts,added.fonts);transfer(root.packages,added.packages);
    transfer(root.screens,added.screens);transfer(root.text,added.text);
    transfer(root.bitmaps,added.bitmaps);transfer(root.modelAssets,added.modelAssets);
    const auto token=block.token;
    root.bundles.push_back(std::move(block));++root.nextBundle;
    return token;
}

bool FlailerResources::CanRemoveBundle(std::uint64_t token) const
{
    auto& root=*storage_;
    const auto found=std::find_if(root.bundles.begin(),root.bundles.end(),
        [token](const auto& value){return value.token==token;});
    if (found==root.bundles.end())return false;
    const auto& block=*found;
    // A hidden/rebuilt object can already have detached its retained strip.
    // Other packages also borrow this bundle's font/texture descriptors.
    // Resource retirement is a frame boundary: retain the whole canonical
    // UI owner until both queued layers and submitted frames have drained.
    if (root.stripPool.HasPendingSubmissions())return false;
    // Live eModels retain these actual solid addresses. Their owners must
    // dispose them before a global/frontend file can release its geometry.
    for (const auto* model=root.modelRegistry->first;model;model=model->next)
        for (auto* assets:block.models) {
            const auto& catalogue=assets->Native();
            for (std::size_t i=0;i<catalogue.solidCount;++i)
                if (model->solid==&catalogue.solids[i])return false;
        }
    return true;
}

bool FlailerResources::RemoveBundle(std::uint64_t token)
{
    if (!CanRemoveBundle(token))return false;
    auto& root=*storage_;
    const auto found=std::find_if(root.bundles.begin(),root.bundles.end(),
        [token](const auto& value){return value.token==token;});
    const auto& block=*found;
    // Retained frames have drained above. Cached strips, bitmap descriptors
    // and font atlases in other bundles can still borrow this file, so detach
    // those caches before its texture/model providers are unpublished.
    DeleteAllStrips();
    for (auto* screen:block.screens) {
        screen->previous->next=screen->next;screen->next->previous=screen->previous;
    }
    for (auto* package:block.packages) {
        package->previous->next=package->next;package->next->previous=package->previous;
    }
    const auto erase=[](auto& objects,const auto& members) {
        objects.erase(std::remove_if(objects.begin(),objects.end(),[&](const auto& value) {
            return std::find(members.begin(),members.end(),value.get())!=members.end();
        }),objects.end());
    };
    erase(root.text,block.text);erase(root.bitmaps,block.bitmaps);
    erase(root.screens,block.screens);erase(root.packages,block.packages);erase(root.fonts,block.fonts);
    erase(root.modelAssets,block.models);
    const auto offset=block.offset,size=block.size;
    root.authored.erase(root.authored.begin()+offset,root.authored.begin()+offset+size);
    for (auto& remaining:root.bundles)if (remaining.offset>offset)remaining.offset-=size;
    root.bundles.erase(found);
    return true;
}

const Font* FlailerResources::FindFont(std::uint32_t key) const noexcept
{
    for (const auto& font:storage_->fonts) if (font->font.key==key) return &font->font;
    storage_->missingFontReported=true;
    return nullptr;
}

bool FlailerResources::MissingFontReported() const noexcept { return storage_->missingFontReported; }

std::size_t FlailerResources::FontCount() const noexcept { return storage_->fonts.size(); }
const Font& FlailerResources::FontAt(std::size_t index) const { return storage_->fonts.at(index)->font; }
std::size_t FlailerResources::ScreenCount() const noexcept { return storage_->screens.size(); }
FlailerScreenResource& FlailerResources::ScreenAt(std::size_t index) { return *storage_->screens.at(index); }
std::size_t FlailerResources::PackageCount() const noexcept { return storage_->packages.size(); }
FlailerPackageResource& FlailerResources::PackageRoot() noexcept { return storage_->packageRoot; }
const std::vector<std::uint8_t>& FlailerResources::Authored() const noexcept { return storage_->authored; }

FlailerScreenResource* FlailerResources::FindScreen(std::uint32_t key)
{
    std::int32_t missed=0;
    auto& root=storage_->packageRoot;
    for (auto* package=root.next;package!=&root;package=package->next) {
        auto& screens=package->screenRoot;
        for (auto* screen=screens.next;screen!=&screens;screen=screen->next,++missed) {
            if (screen->key!=key) continue;
            if (missed>=2) {
                screen->previous->next=screen->next;screen->next->previous=screen->previous;
                screen->next=screens.next;screen->previous=&screens;
                screens.next->previous=screen;screens.next=screen;
                package->previous->next=package->next;package->next->previous=package->previous;
                package->next=root.next;package->previous=&root;
                root.next->previous=package;root.next=package;
            }
            return screen;
        }
    }
    return nullptr;
}

namespace {
constexpr float PcOptionValueCenterX=510.0f;
constexpr float PcOptionArrowSpacing=100.0f;
}

void FlailerResources::CloneOptionsPage(std::uint32_t key,std::size_t rows)
{
    constexpr auto optionsScreen=0x95ad096bu;
    const bool augmentOptions=key==optionsScreen;
    if(FindScreen(key) && (!augmentOptions || FindObject(key,0x20d73d01u)))return;
    auto* source=FindScreen(0x95ad096bu);
    const auto* value=static_cast<const FlailerTextChunk*>(FindObject(0x8bc68340u,0x55cb09f2u,1));
    Require(source && value && rows>=1 && rows<=7,"PC options require the original PAL menu template");
    auto& root=*storage_;
    const auto bundle=std::find_if(root.bundles.begin(),root.bundles.end(),[source](const auto& entry) {
        return std::find(entry.screens.begin(),entry.screens.end(),source)!=entry.screens.end();
    });
    Require(bundle!=root.bundles.end(),"PC options have no frontend bundle owner");
    auto* package=root.packageRoot.next;
    for(;package!=&root.packageRoot;package=package->next) {
        const auto* end=&package->screenRoot;
        auto* screen=end->next;
        for(;screen!=end && screen!=source;screen=screen->next) {}
        if(screen==source)break;
    }
    Require(package!=&root.packageRoot,"PC options have no frontend package");

    auto page=std::make_unique<FlailerScreenResource>();
    page->key=key;page->sharedScreenKey=source->sharedScreenKey;page->visibility=1;
    page->objects.next=page->objects.previous=&page->objects;
    std::vector<std::unique_ptr<FlailerTextChunk>> text;
    std::vector<std::unique_ptr<FlailerBitmapChunk>> bitmaps;
    for(const auto arrow:{0xd1cda594u,0x9345e4a7u,0xedf2e4eeu,0x20d73d01u}) {
        const auto* origin=static_cast<const FlailerBitmapChunk*>(FindObject(0x8bc68340u,arrow,2));
        Require(origin,"The PAL Display page is missing a navigation arrow");
        auto copy=std::make_unique<FlailerBitmapChunk>(*origin);
        copy->clipObjectKey=0;copy->strip=nullptr;copy->visible=0;
        if(!augmentOptions && (arrow==0xd1cda594u || arrow==0x9345e4a7u)) {
            const float offset=arrow==0xd1cda594u?-PcOptionArrowSpacing:PcOptionArrowSpacing;
            copy->parentKey=0;
            copy->topLeftX=PcOptionValueCenterX+offset-ScaledBitmapWidth(*copy)*0.5f;
        }
        bitmaps.push_back(std::move(copy));
    }
    if(augmentOptions) {
        root.bitmaps.reserve(root.bitmaps.size()+bitmaps.size());
        bundle->bitmaps.reserve(bundle->bitmaps.size()+bitmaps.size());
        for(auto& object:bitmaps) {
            appendObject(*source,*object);++source->bitmapCount;
            bundle->bitmaps.push_back(object.get());root.bitmaps.push_back(std::move(object));
        }
        return;
    }
    for(auto& object:bitmaps) {appendObject(*page,*object);++page->bitmapCount;}
    constexpr auto firstItem=0x52e039f3u;
    std::array<const FlailerObjectChunk*,7> labels{};
    for(const auto* object=source->objects.next;object!=&source->objects;object=object->next)
        if(object->kind==1 && object->key>=firstItem && object->key<firstItem+7)
            labels[object->key-firstItem]=object;
    const auto cloneText=[&](const FlailerTextChunk& original) -> FlailerTextChunk& {
        auto copy=std::make_unique<FlailerTextChunk>(original);
        copy->strip=nullptr;copy->hudRow=nullptr;copy->text=copy->ownedText.data();
        appendObject(*page,*copy);++page->textCount;
        text.push_back(std::move(copy));return *text.back();
    };
    for(const auto* object=source->objects.next;object!=&source->objects;object=object->next) {
        if(object->key==0xab671c9au)continue;
        bool skip=object->key==0xaa5d7d3fu && object->topLeftY>100.0f+37.0f*static_cast<float>(rows-1);
        if(object->key==hashString("PC_BACK_TEXT") || object->key==hashString("PC_BACK_ICON"))skip=true;
        for(std::size_t row=rows;row<labels.size();++row)
            if(labels[row] && object->parentKey==labels[row]->parentKey)skip=true;
        if(skip)continue;
        if(object->kind==1) {
            auto& copy=cloneText(static_cast<const FlailerTextChunk&>(*object));
            if(copy.languageId==10005 && copy.topLeftY<30.0f)copy.key=hashString("PC_OPTIONS_TITLE");
        } else if(object->kind==2) {
            auto copy=std::make_unique<FlailerBitmapChunk>(static_cast<const FlailerBitmapChunk&>(*object));
            copy->strip=nullptr;copy->tachometerPoints=nullptr;
            appendObject(*page,*copy);++page->bitmapCount;bitmaps.push_back(std::move(copy));
        }
    }
    for(std::size_t row=0;row<rows;++row) {
        Require(labels[row],"Missing authored PC options row template");
        auto& copy=cloneText(*value);
        copy.key=hashString("PC_OPTION_VALUE")+static_cast<std::uint32_t>(row);
        copy.parentKey=labels[row]->parentKey;copy.topLeftX=PcOptionValueCenterX;
        copy.topLeftY=labels[row]->topLeftY+6.0f;
        copy.formatOptions=(copy.formatOptions&~1u)|4u;
        copy.clipObjectKey=0;
        copy.SetString("");
    }
    root.screens.reserve(root.screens.size()+1);
    root.text.reserve(root.text.size()+text.size());
    root.bitmaps.reserve(root.bitmaps.size()+bitmaps.size());
    bundle->screens.reserve(bundle->screens.size()+1);
    bundle->text.reserve(bundle->text.size()+text.size());
    bundle->bitmaps.reserve(bundle->bitmaps.size()+bitmaps.size());
    auto& ring=package->screenRoot;
    page->next=&ring;page->previous=ring.previous;
    page->previous->next=page.get();ring.previous=page.get();
    bundle->screens.push_back(page.get());root.screens.push_back(std::move(page));
    for(auto& object:text) {bundle->text.push_back(object.get());root.text.push_back(std::move(object));}
    for(auto& object:bitmaps) {bundle->bitmaps.push_back(object.get());root.bitmaps.push_back(std::move(object));}
}

FlailerTextChunk& FlailerResources::CloneNativeText(std::uint32_t screen,const FlailerTextChunk& source,std::uint32_t key)
{
    if(auto* existing=FindObject(screen,key,1))return static_cast<FlailerTextChunk&>(*existing);
    auto* page=FindScreen(screen);
    auto& root=*storage_;
    const auto bundle=std::find_if(root.bundles.begin(),root.bundles.end(),[page](const auto& entry) {
        return std::find(entry.screens.begin(),entry.screens.end(),page)!=entry.screens.end();
    });
    Require(page && bundle!=root.bundles.end(),"Native menu text has no frontend bundle owner");
    auto copy=std::make_unique<FlailerTextChunk>(source);
    copy->key=key;copy->strip=nullptr;copy->hudRow=nullptr;copy->text=copy->ownedText.data();
    root.text.reserve(root.text.size()+1);
    bundle->text.reserve(bundle->text.size()+1);
    appendObject(*page,*copy);++page->textCount;
    bundle->text.push_back(copy.get());root.text.push_back(std::move(copy));
    return *root.text.back();
}

void FlailerResources::AddNativeBackHints()
{
    const auto options=FindScreen(0x95ad096bu);
    const auto bundle=std::find_if(storage_->bundles.begin(),storage_->bundles.end(),[options](const auto& entry) {
        return std::find(entry.screens.begin(),entry.screens.end(),options)!=entry.screens.end();
    });
    if(bundle==storage_->bundles.end())return;
    const auto backIcon=hashString("PC_BACK_ICON"),backText=hashString("PC_BACK_TEXT");
    const auto buttons=hashString("BUTTONSOUT");
    for(auto* page:bundle->screens) {
        if(page->key==0xae0f8aa1u || FindObject(page->key,backText,1))continue;
        const FlailerTextChunk* helpIcon=nullptr;
        const FlailerTextChunk* helpText=nullptr;
        for(auto* object=page->objects.next;object!=&page->objects;object=object->next) {
            if(object->kind!=1 || object->topLeftY<400.0f)continue;
            const auto* field=static_cast<const FlailerTextChunk*>(object);
            if(field->languageId==12000)helpText=field;
            if(!field->languageId && field->fontKey==buttons && std::strcmp(field->text,"k")==0)helpIcon=field;
        }
        if(!helpIcon || !helpText)continue;
        auto& icon=CloneNativeText(page->key,*helpIcon,backIcon);
        auto& text=CloneNativeText(page->key,*helpText,backText);
        icon.SetTopLeftX(helpIcon->topLeftX+88.0f);
        icon.SetString("\x87");
        text.SetTopLeftX(helpText->topLeftX+88.0f);
        SetLocalizedText(page->key,backText,"Back");
    }
}

void FlailerResources::CloneProfileKeyboard(std::uint32_t screen)
{
    constexpr std::uint32_t anchor=0x35b2f256u,keyboardGroup=0x670b6fddu;
    constexpr std::uint32_t cells=0x7b95e55cu,caret=0xc01086b5u;
    if(FindObject(screen,anchor,2))return;
    auto* page=FindScreen(screen);
    auto* source=FindScreen(0x617a4e0au);
    auto& root=*storage_;
    const auto bundle=std::find_if(root.bundles.begin(),root.bundles.end(),[page](const auto& entry) {
        return std::find(entry.screens.begin(),entry.screens.end(),page)!=entry.screens.end();
    });
    Require(page && source && bundle!=root.bundles.end(),"Profile editor has no authored keyboard template");
    for(const auto* object=source->objects.next;object!=&source->objects;object=object->next) {
        if(object->parentKey!=keyboardGroup && object->parentKey!=cells && object->key!=caret)continue;
        if(object->kind==1) {
            CloneNativeText(screen,static_cast<const FlailerTextChunk&>(*object),object->key).visible=0;
        } else if(object->kind==2) {
            auto bitmap=std::make_unique<FlailerBitmapChunk>(static_cast<const FlailerBitmapChunk&>(*object));
            bitmap->strip=nullptr;bitmap->visible=0;
            root.bitmaps.reserve(root.bitmaps.size()+1);bundle->bitmaps.reserve(bundle->bitmaps.size()+1);
            appendObject(*page,*bitmap);++page->bitmapCount;
            bundle->bitmaps.push_back(bitmap.get());root.bitmaps.push_back(std::move(bitmap));
        }
    }
    Require(FindObject(screen,anchor,2),"Profile keyboard is missing its authored anchor");
}

void FlailerResources::AddNativeAudioMaster(std::uint32_t screen)
{
    const auto master=hashString("MENUITEM_0");
    if(FindObject(screen,master))return;
    auto* page=FindScreen(screen);
    if(!page)return;
    auto& root=*storage_;
    const auto bundle=std::find_if(root.bundles.begin(),root.bundles.end(),[page](const auto& entry) {
        return std::find(entry.screens.begin(),entry.screens.end(),page)!=entry.screens.end();
    });
    Require(bundle!=root.bundles.end(),"Audio master volume requires the PAL Audio menu");
    auto* first=static_cast<FlailerTextChunk*>(FindObject(screen,0x52e039f3u,1));
    auto* bar=static_cast<FlailerBitmapChunk*>(FindObject(screen,0xa29f7a16u,2));
    auto* marker=static_cast<FlailerBitmapChunk*>(FindObject(screen,0xc866642bu,2));
    const auto* firstMask=static_cast<const FlailerBitmapChunk*>(FindObject(screen,0x6e34e350u,2));
    Require(first && bar && marker && firstMask,"Audio master row is missing its slider template");
    auto text=std::make_unique<FlailerTextChunk>(*first);
    text->strip=nullptr;text->text=text->ownedText.data();text->languageId=0;text->key=master;
    const auto group=hashString("MENUITEM_0_GROUP");
    text->parentKey=group;
    auto base=std::make_unique<FlailerBitmapChunk>(*bar);
    base->key=hashString("PC_MASTER_BAR");base->strip=nullptr;base->parentKey=group;
    auto handle=std::make_unique<FlailerBitmapChunk>(*marker);
    handle->key=hashString("PC_MASTER_MARKER");handle->strip=nullptr;handle->parentKey=group;
    root.text.reserve(root.text.size()+1);bundle->text.reserve(bundle->text.size()+1);
    auto mask=std::make_unique<FlailerBitmapChunk>(*firstMask);
    mask->key=hashString("MI_0_MASK");mask->strip=nullptr;mask->parentKey=group;
    root.bitmaps.reserve(root.bitmaps.size()+3);bundle->bitmaps.reserve(bundle->bitmaps.size()+3);
    for(auto* object=page->objects.next;object!=&page->objects;object=object->next) {
        if(object->parentKey==0x457755c9u)continue;
        if(object->topLeftY>=70.0f && object->topLeftY<450.0f &&
           (object->parentKey==0xa6bbd61fu || object->clipObjectKey==0xc67cfb4au ||
            std::strncmp(object->name.data(),"MENUITEM_",9)==0 ||
            std::strncmp(object->name.data(),"AUDIO_BAR",9)==0 ||
            std::strncmp(object->name.data(),"MI_",3)==0 ||
            object->key==0x2932dfcau))object->SetTopLeftY(object->topLeftY+37.0f);
    }
    appendObject(*page,*text);++page->textCount;
    bundle->text.push_back(text.get());root.text.push_back(std::move(text));
    for(auto* slider:{&base,&handle,&mask}) {
        appendObject(*page,**slider);++page->bitmapCount;
        bundle->bitmaps.push_back(slider->get());root.bitmaps.push_back(std::move(*slider));
    }
}

void FlailerResources::AddNativeOptionSliders(std::uint32_t screen,std::size_t rows)
{
    const auto firstBar=hashString("PC_OPTION_SLIDER_BAR");
    if(FindObject(screen,firstBar,2))return;
    auto* page=FindScreen(screen);
    const auto* track=static_cast<const FlailerBitmapChunk*>(FindObject(0xe116761cu,0xa29f7a16u,2));
    const auto* thumb=static_cast<const FlailerBitmapChunk*>(FindObject(0xe116761cu,0xc866642bu,2));
    Require(page && track && thumb,"Options sliders require the Audio artwork");
    auto& root=*storage_;
    const auto bundle=std::find_if(root.bundles.begin(),root.bundles.end(),[page](const auto& entry) {
        return std::find(entry.screens.begin(),entry.screens.end(),page)!=entry.screens.end();
    });
    Require(bundle!=root.bundles.end(),"Options sliders have no screen bundle owner");
    bundle->bitmaps.reserve(bundle->bitmaps.size()+2*rows);
    root.bitmaps.reserve(root.bitmaps.size()+2*rows);
    for(std::size_t row=0;row<rows;++row) {
        const auto offset=static_cast<std::uint32_t>(row);
        const auto* label=FindObject(screen,0x52e039f3u+offset,1);
        Require(label,"Slider row has no label");
        for(const auto* artwork:{track,thumb}) {
            auto image=std::make_unique<FlailerBitmapChunk>(*artwork);
            image->key=(artwork==track?firstBar:hashString("PC_OPTION_SLIDER_THUMB"))+offset;
            image->clipObjectKey=0;image->strip=nullptr;image->visible=0;image->nativeSliderItem=0;
            image->parentKey=label->parentKey;
            image->topLeftX=PcOptionValueCenterX-ScaledBitmapWidth(*image)*0.5f;
            image->topLeftY=label->topLeftY+14.0f+artwork->topLeftY-track->topLeftY;
            appendObject(*page,*image);++page->bitmapCount;
            bundle->bitmaps.push_back(image.get());root.bitmaps.push_back(std::move(image));
        }
    }
}

platform::NativePromptDevice FlailerResources::PromptDevice() const noexcept
{return storage_->promptDevice;}

void FlailerResources::SetPromptDevice(platform::NativePromptDevice device)
{
    if(storage_->promptDevice==device)return;
    storage_->promptDevice=device;
    for(const auto& text:storage_->text)if(!text->nativePhysicalButtonLabel)text->DeleteFlailerStrip();
    static const auto replayToolbar=hashString("REPLAYBOTTOM_NEW");
    for(const auto& bitmap:storage_->bitmaps)
        if(bitmap->textureKey==replayToolbar)bitmap->DeleteFlailerStrip();
}

FlailerObjectChunk* FlailerResources::FindObject(std::uint32_t screenKey,std::uint32_t objectKey,std::int32_t kind)
{
    auto* screen=FindScreen(screenKey);
    if (!screen) return nullptr;
    for (auto* object=screen->objects.next;object!=&screen->objects;object=object->next)
        if (object->key==objectKey && (!kind || static_cast<std::int32_t>(object->kind)==kind)) return object;
    return nullptr;
}

void FlailerResources::SetTextColor(std::uint32_t screenKey,std::uint32_t objectKey,std::uint32_t rgb)
{
    if (auto* object=FindObject(screenKey,objectKey,1))
        static_cast<FlailerTextChunk*>(object)->SetColor(rgb);
}

void FlailerTextChunk::SetString(const char* value)
{
    Require(value && text && textCapacity && ownedText.size()>textCapacity,
        "Flailer text has no retained counted buffer");
    if (std::strcmp(value,text)==0) return;
    updateKind=5;
    // PAL uses a counted memcpy and may read allocation bytes beyond the
    // input terminator. Native C-string storage preserves visible characters,
    // including forward aliases in this buffer, and zeroes private tail
    // padding instead of reading an unknown caller allocation tail.
    bool ended=false;
    for (std::size_t index=0;index<textCapacity;++index) {
        const char character=ended ? '\0' : value[index];
        ownedText[index]=character;
        if (!character) ended=true;
    }
    ownedText[static_cast<std::size_t>(textCapacity)-1]='\0';
    languageId=0;
}

void FlailerTextChunk::SetLanguageID(std::uint32_t value)
{
    if (languageId==value) return;
    Require(!ownedText.empty(),"Flailer language text has no retained buffer");
    languageId=value;updateKind=5;ownedText[0]='\0';
    UpdateLanguageText();
}

std::int32_t FlailerResources::Printf(std::uint32_t screenKey,std::uint32_t objectKey,const char* format,...)
{
    auto* object=FindObject(screenKey,objectKey,1);
    if (!object) return 0;
    Require(format,"Flailer formatted text has no format owner");
    char buffer[128];
    va_list arguments;
    va_start(arguments,format);
    const auto length=std::vsnprintf(buffer,sizeof(buffer),format,arguments);
    va_end(arguments);
    Require(length>=0,"Flailer text formatting failed");
    if(static_cast<std::size_t>(length)<sizeof(buffer)) {
        static_cast<FlailerTextChunk*>(object)->SetString(buffer);
    } else {
        // Keep the hot short-text path unchanged. Localized output is not
        // limited by this scratch array; the actual UI field owns its size.
        std::string text;
        va_start(arguments,format);
        try {text=FormatTextV(format,arguments);}
        catch(...) {va_end(arguments);throw;}
        va_end(arguments);
        static_cast<FlailerTextChunk*>(object)->SetString(text.c_str());
    }
    return length;
}

std::int32_t FlailerResources::SetLocalizedText(std::uint32_t screenKey,std::uint32_t objectKey,const char* value)
{
    auto* object=FindObject(screenKey,objectKey,1);
    if(!object)return 0;
    Require(value,"Localized frontend field has no completed text");
    auto& field=*static_cast<FlailerTextChunk*>(object);
    const auto length=std::strlen(value);
    Require(length<static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()),
        "Localized frontend field exceeds its native counted extent");
    if(length>=field.textCapacity) {
        // Own aliases before vector growth can retire their backing storage.
        const std::string retained(value);
        field.ownedText.resize(length+2u,0);
        field.text=field.ownedText.data();
        field.textCapacity=static_cast<std::uint32_t>(length+1u);
        field.SetString(retained.c_str());
    } else field.SetString(value);
    return static_cast<std::int32_t>(length);
}

void FlailerResources::SetFittedText(std::uint32_t screen,std::uint32_t key,const LanguageManager* language,
    const char* value,std::uint32_t followingColumn)
{
    auto* text=static_cast<FlailerTextChunk*>(FindObject(screen,key,1));
    if(!text)return;
    SetLocalizedText(screen,key,value);
    if(!text->nativeFitBaseX) {
        text->nativeFitBaseX=text->scaleX;text->nativeFitBaseY=text->scaleY;
    }
    text->SetScaleX(text->nativeFitBaseX);text->SetScaleY(text->nativeFitBaseY);
    float left=4.0f,right=636.0f;
    int clipLeft{},clipTop{},clipRight{},clipBottom{};
    if(text->clipObjectKey && GetBoundingBox(screen,text->clipObjectKey,clipLeft,clipTop,clipRight,clipBottom,language)) {
        left=static_cast<float>(clipLeft)+5.0f;right=static_cast<float>(clipRight)-2.0f;
    }
    if(const auto* next=followingColumn?FindObject(screen,followingColumn,1):nullptr) {
        float x{},y{};
        const auto& column=static_cast<const FlailerTextChunk&>(*next);
        if(column.formatOptions&(1u|4u))GetTopLeft(column,x,y,language);
        else x=column.topLeftX;
        if(x>text->topLeftX)right=std::min(right,x-4.0f);
    }
    float width=right-text->topLeftX;
    if(text->formatOptions&1u)width=text->topLeftX-left;
    else if(text->formatOptions&4u)width=2.0f*std::min(width,text->topLeftX-left);
    const auto measured=ScaledTextWidth(*text,language);
    if(width>0 && measured>width) {
        const auto scale=width/measured;
        text->SetScaleX(text->nativeFitBaseX*scale);text->SetScaleY(text->nativeFitBaseY*scale);
    }
}

void FlailerResources::SetObjectVisibility(std::uint32_t screenKey,std::uint32_t objectKey,std::int32_t value)
{
    auto* object=FindObject(screenKey,objectKey);
    if (!object) return;
    object->visible=static_cast<std::uint8_t>(value);
    if (!value && object->strip) object->strip->pool->Discard(*object);
}

void FlailerResources::SetTopLeftPosition(std::uint32_t screenKey,std::uint32_t objectKey,
    std::int32_t x,std::int32_t y)
{
    if (auto* object=FindObject(screenKey,objectKey)) {
        object->SetTopLeftX(static_cast<float>(x));
        object->SetTopLeftY(static_cast<float>(y));
    }
}

void FlailerResources::SetTopLeftX(std::uint32_t screenKey,std::uint32_t objectKey,std::int32_t x)
{
    if (auto* object=FindObject(screenKey,objectKey)) object->SetTopLeftX(static_cast<float>(x));
}

void FlailerResources::GetTopLeft(const FlailerObjectChunk& object,float& x,float& y,
    const LanguageManager* language) const
{
    if (object.kind==1) {
        const auto& text=static_cast<const FlailerTextChunk&>(object);
        const float width=ScaledTextWidth(text,language);
        if (text.formatOptions&4u) {
            x=object.topLeftX-width*0.5f;
            y=object.topLeftY;
            return;
        }
        x=(text.formatOptions&1u) ? object.topLeftX-width : object.topLeftX;
    } else x=object.topLeftX;
    y=object.topLeftY;
}

bool FlailerResources::GetBoundingBox(std::uint32_t screenKey,std::uint32_t objectKey,
    std::int32_t& left,std::int32_t& top,std::int32_t& right,std::int32_t& bottom,
    const LanguageManager* language)
{
    auto* object=FindObject(screenKey,objectKey);
    if (!object) return false;
    std::int32_t width=0,height=0;
    if (object->kind==1) {
        const auto& text=static_cast<const FlailerTextChunk&>(*object);
        width=convertFloatToInt32(ScaledTextWidth(text,language));
        height=convertFloatToInt32(ScaledTextHeight(text));
    } else if (object->kind==2) {
        auto& image=static_cast<FlailerBitmapChunk&>(*object);
        width=convertFloatToInt32(ScaledBitmapWidth(image));
        height=convertFloatToInt32(ScaledBitmapHeight(image));
    }
    float x=0.0f,y=0.0f;
    if (auto* positioned=FindObject(screenKey,objectKey)) GetTopLeft(*positioned,x,y,language);
    const auto originX=convertFloatToInt32(x),originY=convertFloatToInt32(y);
    const auto add=[](std::int32_t first,std::int32_t second) {
        const auto bits=static_cast<std::uint32_t>(first)+static_cast<std::uint32_t>(second);
        std::int32_t result;std::memcpy(&result,&bits,sizeof(result));return result;
    };
    // Keep the original four stores, including when output references alias.
    left=originX;
    right=add(originX,width);
    top=originY;
    bottom=add(originY,height);
    return true;
}

void FlailerResources::SetGroupVisibility(std::uint32_t screenKey,std::uint32_t groupKey,std::int32_t value)
{
    // The first lookup's result is deliberately discarded in the original.
    // Keep its resource-cache access ahead of the ordered traversal.
    (void)FindObjectByGroup(screenKey,groupKey,0);
    FlailerObjectChunk* previous=nullptr;
    while ((previous=FindObjectByGroup(screenKey,groupKey,0,previous))!=nullptr)
        previous->visible=static_cast<std::uint8_t>(value);
}

void FlailerObjectChunk::SetAlpha(std::int32_t alpha)
{
    if (kind==1)static_cast<FlailerTextChunk*>(this)->SetAlpha(alpha);
    else if (kind==2)static_cast<FlailerBitmapChunk*>(this)->SetAlpha(alpha,-1);
}

void FlailerResources::SetGroupAlpha(std::uint32_t screenKey,std::uint32_t groupKey,std::int32_t alpha)
{
    auto* object=FindObjectByGroup(screenKey,groupKey,0);
    while (object) {
        object->SetAlpha(alpha);
        object=FindObjectByGroup(screenKey,groupKey,0,object);
    }
}

void FlailerResources::MoveGroupTo(std::uint32_t screenKey,std::uint32_t groupKey,std::uint32_t anchorKey,float x,float y)
{
    auto* anchor=FindObject(screenKey,anchorKey);
    if (!anchor) return;
    const float offsetX=x-anchor->topLeftX;
    const float offsetY=y-anchor->topLeftY;
    auto* screen=FindScreen(screenKey);
    if (!screen) return;
    for (auto* object=screen->objects.previous;object!=&screen->objects;object=object->previous) {
        if (object->parentKey!=groupKey) continue;
        object->SetTopLeftX(object->topLeftX+offsetX);
        object->SetTopLeftY(object->topLeftY+offsetY);
    }
}

FlailerObjectChunk* FlailerResources::FindObjectByGroup(std::uint32_t screenKey,std::uint32_t groupKey,
    std::int32_t kind,FlailerObjectChunk* previous)
{
    auto* screen=FindScreen(screenKey);
    if (!screen) return nullptr;
    auto* first=previous ? previous->next : screen->objects.next;
    for (auto* object=first;object!=&screen->objects;object=object->next)
        if (object->parentKey==groupKey && (!kind || static_cast<std::int32_t>(object->kind)==kind)) return object;
    return nullptr;
}

float FlailerResources::ScaledTextHeight(const FlailerTextChunk& text) const
{
    const auto* font=FindFont(text.fontKey);
    if (!font) return 0.0f;
    return text.scaleY*static_cast<float>(convertFloatToInt32(font->lineHeight));
}

namespace {
platform::NativePromptDevice nativeHelpDevice(const FlailerResources& resources,const FlailerTextChunk& text)
{
    return text.nativePhysicalButtonLabel?text.nativePhysicalButtonDevice:resources.PromptDevice();
}

std::uint8_t nativeHelpCharacter(platform::NativePromptDevice device,const Font& font,std::uint8_t character)
{
    static const auto buttonFont=hashString("BUTTONSOUT");
    if(device==platform::NativePromptDevice::Original ||
        font.key!=buttonFont || IsHelpCharacter(character))return character;
    const auto& glyphs=flailerTextRasterState.helpGlyphs;
    const auto found=std::find(glyphs.begin(),glyphs.end(),character);
    return found==glyphs.end()?character:static_cast<std::uint8_t>(0x80u+(found-glyphs.begin()));
}

const char* nativeHelpLabel(platform::NativePromptDevice device,std::uint8_t character)
{
    constexpr const char* keyboard[]{"Enter","BS","UP","DN","LT","RT","Enter","Esc","Space","F1",
        "Q","E","1","2","L3","N","WASD","W","S","A","D","ARWS","UP","DN","LT",nullptr,
        nullptr,nullptr,"Del",nullptr};
    constexpr const char* xbox[]{"Menu","View","Up","Down","Left","Right","A","B","X","Y","LB","RB","LT",
        "RT","LS","RS","LS","LS","LS","LS","LS","RS","RS","RS","RS",nullptr,
        nullptr,nullptr,nullptr,nullptr};
    if(!IsHelpCharacter(character))return nullptr;
    const auto index=character-0x80u;
    if(device==platform::NativePromptDevice::Keyboard)return keyboard[index];
    if(device==platform::NativePromptDevice::Xbox)return xbox[index];
    return nullptr;
}

float nativeHelpWidth(platform::NativePromptDevice device,std::uint8_t character,const char* label)
{
    if(device==platform::NativePromptDevice::Xbox && character>=0x80 && character<=0x89)return 24.0f;
    return std::max(19.0f,10.0f+6.0f*static_cast<float>(std::strlen(label)));
}

float unscaledFlailerTextWidth(const FlailerResources& resources,const FlailerTextChunk& text,
    const LanguageManager* language)
{
    const auto* font=resources.FindFont(text.fontKey);
    if (!font) return 0.0f;
    const char* value=text.text;
    Require(value,"Flailer text width has no string owner");
    if (language && language->textEnabled && text.languageId) {
        const auto* translated=language->GetString(static_cast<std::int32_t>(text.languageId));
        // PAL's GetLanguageString supplies an empty global string on a miss.
        value=translated ? translated : "";
    }
    float width=0.0f;
    const auto device=nativeHelpDevice(resources,text);
    for (const auto* cursor=reinterpret_cast<const unsigned char*>(value);*cursor;++cursor) {
        const auto prompt=nativeHelpCharacter(device,*font,*cursor);
        if(const auto* label=nativeHelpLabel(device,prompt))
            width+=nativeHelpWidth(device,prompt,label)/std::max(0.01f,text.scaleX);
        else if (const auto* glyph=font->characters[*cursor]) width+=glyph->advance;
    }
    return width;
}
}

float FlailerResources::ScaledTextWidth(const FlailerTextChunk& text,const LanguageManager* language) const
{
    return text.scaleX*unscaledFlailerTextWidth(*this,text,language);
}

void FlailerResources::ScaleTextAboutCenter(FlailerTextChunk& text,float scale,const LanguageManager* language) const
{
    text.updateKind=3;
    // NIS/HUD envelopes can begin at scale zero on PC. Use the unscaled
    // authored font extent at that endpoint instead of producing 0/0 and
    // permanently poisoning the retained position with NaN. Keep the
    // released arithmetic unchanged for every nonzero source scale.
    const auto* font=(text.scaleX==0.0f || text.scaleY==0.0f)?FindFont(text.fontKey):nullptr;
    const float halfWidth=text.scaleX!=0.0f ? ScaledTextWidth(text,language)/(2.0f*text.scaleX):
        unscaledFlailerTextWidth(*this,text,language)*0.5f;
    const float halfHeight=text.scaleY!=0.0f ? ScaledTextHeight(text)/(2.0f*text.scaleY):
        (font?static_cast<float>(convertFloatToInt32(font->lineHeight))*0.5f:0.0f);
    if (text.formatOptions&2u) {
        text.topLeftX+=halfWidth*(text.scaleX-scale);
        text.topLeftY+=halfHeight*(text.scaleY-scale);
    } else if (text.formatOptions&4u) text.topLeftY+=halfHeight*(text.scaleY-scale);
    else if (text.formatOptions&1u) {
        text.topLeftX-=halfWidth*(text.scaleX-scale);
        text.topLeftY+=halfHeight*(text.scaleX-scale);
    }
    text.scaleX=scale;text.scaleY=scale;
}

// PAL141588: raw signed texture dimensions keep the center fixed even when
// the old scale is zero. Dividing ScaledBitmapWidth by scale is not equivalent.
void FlailerResources::ScaleBitmapAboutCenter(FlailerBitmapChunk& bitmap,float scale) const
{
    bitmap.updateKind=3;
    if (!bitmap.texture) {
        bitmap.texture=FindTexture(bitmap.textureKey);
        bitmap.textureStorage=textureMetadataOwner(bitmap.texture);
    }
    Require(bitmap.texture && bitmap.texture->authoredRecord && bitmap.texture->authoredRecordSize>=40,
        "Flailer bitmap has no retained texture dimensions");
    const auto rawWidth=ReadU16(bitmap.texture->authoredRecord+36);
    const auto rawHeight=ReadU16(bitmap.texture->authoredRecord+38);
    std::int16_t width,height;
    std::memcpy(&width,&rawWidth,sizeof(width));
    std::memcpy(&height,&rawHeight,sizeof(height));
    const float halfWidth=static_cast<float>(width)*0.5f;
    const float halfHeight=static_cast<float>(height)*0.5f;
    bitmap.topLeftX+=halfWidth*(bitmap.scaleX-scale);
    bitmap.topLeftY+=halfHeight*(bitmap.scaleY-scale);
    bitmap.scaleX=scale;bitmap.scaleY=scale;
}

// PAL140820: other authored object kinds deliberately have no scale branch.
void FlailerObjectChunk::ScaleAboutCenter(FlailerResources& resources,float scale,
    const LanguageManager* language)
{
    if (kind==1) resources.ScaleTextAboutCenter(static_cast<FlailerTextChunk&>(*this),scale,language);
    else if (kind==2) resources.ScaleBitmapAboutCenter(static_cast<FlailerBitmapChunk&>(*this),scale);
}

// PAL1445B0: the normal ordered object lookup retains its missing-object case.
void FlailerResources::ScaleObjectAboutCenter(std::uint32_t screen,std::uint32_t object,
    float scale,const LanguageManager* language)
{
    if (auto* found=FindObject(screen,object)) found->ScaleAboutCenter(*this,scale,language);
}

void FlailerTextChunk::CopyAttributesFrom(const FlailerTextChunk& source)
{
    // The source memcpy copies its semantic attribute region, including four
    // inline string bytes, then restores the destination's geometry-cache
    // pointer. Neither a console pointer nor a native owning vector is copied.
    Require(ownedText.size()>=4 && source.ownedText.size()>=4,
        "Flailer attribute copy has no four-byte inline string prefix");
    std::array<char,4> prefix{};std::memcpy(prefix.data(),source.ownedText.data(),4);
    // Authored echo objects can have a smaller original text capacity than
    // the selected label (for example 5 versus18). The source only copies
    // four inline bytes here; capacity equality is not its precondition.
    // Grow native backing storage, retaining its existing terminator/tail,
    // before assigning that copied logical capacity.
    if (ownedText.size()<=source.textCapacity)
        ownedText.resize(static_cast<std::size_t>(source.textCapacity)+1u,0);
    text=ownedText.data();
    kind=source.kind;visible=source.visible;authoredFlags=source.authoredFlags;
    topLeftX=source.topLeftX;topLeftY=source.topLeftY;scaleX=source.scaleX;scaleY=source.scaleY;
    rotation=source.rotation;authoredMetrics=source.authoredMetrics;clipObjectKey=source.clipObjectKey;
    fontKey=source.fontKey;textCapacity=source.textCapacity;formatOptions=source.formatOptions;
    color=source.color;languageId=source.languageId;
    std::memcpy(ownedText.data(),prefix.data(),4);updateKind=8;
}

const EditorMarkerTexture* FlailerResources::FindTexture(std::uint32_t key) const
{
    for (std::size_t index=0;index<storage_->textureCount;++index)
        if (storage_->textures[index].key==key) return &storage_->textures[index];
    auto* root=storage_->modelRegistry;
    if (root->catalogueRoot) root=root->catalogueRoot;
    for (auto* catalogue=root;catalogue;catalogue=catalogue->nextCatalogue)
        for (std::size_t index=0;index<catalogue->textureCount;++index)
            if (catalogue->textures[index].key==key) return &catalogue->textures[index];
    return nullptr;
}

EditorMarkerResources& FlailerResources::Models() noexcept
{
    return *storage_->modelRegistry;
}

// Complete PAL1414C8/141528. The original missing-texture branch returns
// without changing scale; dimensions are signed authored16 values.
void FlailerBitmapChunk::SetWidth(FlailerResources& resources,float width)
{
    if(!texture) {texture=resources.FindTexture(textureKey);textureStorage=textureMetadataOwner(texture);}
    if(!texture)return;
    Require(texture->authoredRecord && texture->authoredRecordSize>=40,
        "Flailer bitmap width has no retained texture dimensions");
    const auto raw=ReadU16(texture->authoredRecord+36);
    std::int16_t dimension;std::memcpy(&dimension,&raw,sizeof(dimension));
    SetScaleX(width/static_cast<float>(dimension));
}
void FlailerBitmapChunk::SetHeight(FlailerResources& resources,float height)
{
    if(!texture) {texture=resources.FindTexture(textureKey);textureStorage=textureMetadataOwner(texture);}
    if(!texture)return;
    Require(texture->authoredRecord && texture->authoredRecordSize>=40,
        "Flailer bitmap height has no retained texture dimensions");
    const auto raw=ReadU16(texture->authoredRecord+38);
    std::int16_t dimension;std::memcpy(&dimension,&raw,sizeof(dimension));
    SetScaleY(height/static_cast<float>(dimension));
}

float FlailerResources::ScaledBitmapWidth(FlailerBitmapChunk& bitmap) const
{
    if (!bitmap.texture) {
        bitmap.texture=FindTexture(bitmap.textureKey);
        bitmap.textureStorage=textureMetadataOwner(bitmap.texture);
    }
    Require(bitmap.texture && bitmap.texture->authoredRecord && bitmap.texture->authoredRecordSize>=40,
        "Flailer bitmap has no retained texture dimensions");
    const auto raw=ReadU16(bitmap.texture->authoredRecord+36);
    std::int16_t width;std::memcpy(&width,&raw,sizeof(width));
    return static_cast<float>(width)*bitmap.scaleX;
}

float FlailerResources::ScaledBitmapHeight(FlailerBitmapChunk& bitmap) const
{
    if (!bitmap.texture) {
        bitmap.texture=FindTexture(bitmap.textureKey);
        bitmap.textureStorage=textureMetadataOwner(bitmap.texture);
    }
    Require(bitmap.texture && bitmap.texture->authoredRecord && bitmap.texture->authoredRecordSize>=40,
        "Flailer bitmap has no retained texture dimensions");
    const auto raw=ReadU16(bitmap.texture->authoredRecord+38);
    std::int16_t height;std::memcpy(&height,&raw,sizeof(height));
    return static_cast<float>(height)*bitmap.scaleY;
}

void FlailerResources::SetLanguageID(std::uint32_t screenKey,std::uint32_t objectKey,std::uint32_t value)
{
    if (auto* object=FindObject(screenKey,objectKey,1)) static_cast<FlailerTextChunk*>(object)->SetLanguageID(value);
}

std::int32_t FlailerResources::RenderTextIntoGroup(std::uint32_t screenKey,std::uint32_t groupKey,const char* message)
{
    std::array<FlailerTextChunk*,33> lines{};
    std::size_t count=0;
    FlailerObjectChunk* previous=nullptr;
    while (count<32) {
        previous=FindObjectByGroup(screenKey,groupKey,1,previous);
        if (!previous) break;
        lines[count++]=static_cast<FlailerTextChunk*>(previous);
    }
    if (!count) return 0;
    Require(message,"Flailer message group has no string owner");
    const char* const end=message+std::strlen(message);
    const char* cursor=message;
    const char* start=message;
    const char* lastBoundary=message;
    std::size_t line=0,nextLine=1;
    std::int32_t characters=0;
    bool hasBoundary=false;
    const auto writeLine=[](FlailerTextChunk& text,const char* first,const char* last) {
        Require(first<=last,"Flailer line has an invalid text span");
        const auto size=static_cast<std::size_t>(last-first);
        text.visible=1;
        if(size<256) {
            char buffer[256]{};
            std::memcpy(buffer,first,size);text.SetString(buffer);
        } else {
            const std::string line(first,size);text.SetString(line.c_str());
        }
    };
    while (cursor<end && *cursor) {
        const auto character=*cursor;
        if (character==' ' || character=='\t' || character=='@') {
            lastBoundary=cursor;hasBoundary=true;
        }
        const auto capacity=static_cast<std::int32_t>(lines[line]->textCapacity);
        if (hasBoundary && (characters>=capacity-2 || character=='@')) {
            writeLine(*lines[line],start,lastBoundary);
            characters=0;line=nextLine;
            cursor=lastBoundary+1;start=cursor;
            if (!lines[line]) return 0;
            hasBoundary=false;nextLine=line+1;
        } else ++characters;
        // Preserve PAL's post-break scan step: the first byte remains in the
        // next copied line while its boundary scan starts one byte later.
        ++cursor;
    }
    // An authored terminal '@' can advance the scan beyond the terminator.
    // Its visible final line is empty; keep the host span inside the string.
    writeLine(*lines[line],start,cursor>end ? end : cursor);
    auto* heightLine=lines[nextLine] ? lines[nextLine] : lines[nextLine-1];
    const auto bottom=convertFloatToInt32(heightLine->topLeftY+ScaledTextHeight(*heightLine));
    for (std::size_t unused=nextLine;unused<count;++unused) lines[unused]->visible=0;
    return bottom;
}

void FlailerScreenResource::NotifyVisibilityChange(std::int32_t value)
{
    if (value==1) {
        for (auto* object=objects.next;object!=&objects;object=object->next)
            if (object->strip) object->strip->pool->Discard(*object);
    }
    const auto bits=static_cast<std::uint16_t>(value);
    std::memcpy(&visibility,&bits,sizeof(visibility));
}

void FlailerRenderManager::SetScreenVisibility(FlailerScreenResource* screen,std::int32_t visibility)
{
    if (screen->visibility==visibility) return;
    for (auto& visible:visibleScreens) {
        if (visible==screen) { visible=nullptr;break; }
    }
    if (!visibility) {
        for (auto& visible:visibleScreens) {
            if (!visible) { visible=screen;break; }
        }
    }
    screen->NotifyVisibilityChange(visibility);
}

void FlailerRenderManager::SetScreenVisibility(FlailerResources& resources,std::uint32_t key,std::int32_t visibility)
{
    if (auto* screen=resources.FindScreen(key)) SetScreenVisibility(screen,visibility);
}

FlailerStrip* FlailerStripPool::Create(FlailerObjectChunk& object,std::vector<FlailerPoly> polygons)
{
    Discard(object);
    auto strip=std::make_unique<FlailerStrip>();
    strip->pool=this;strip->polygons=std::move(polygons);
    strip->uiPlacement=object.uiPlacement;
    // Race-over text can also be submitted by the result screen owner.
    if (object.kind==1 && object.parentKey==0xC707E093u)
        strip->uiPlacement=EcstasyUiPlacement::CenteredCanvas;
    // Publish only after the pool accepted ownership. A failed vector growth
    // must not leave object.strip pointing into the destroyed local owner.
    auto* result=strip.get();strips_.push_back(std::move(strip));
    object.strip=result;return result;
}

void FlailerStripPool::Retain(FlailerStrip& strip)
{
    Require(strip.pool==this && !strip.deleteRequested && strip.retentionCount!=UINT32_MAX,
        "Flailer strip cannot be retained");
    ++strip.retentionCount;
}

bool FlailerStripPool::HasPendingSubmissions() const noexcept
{
    for (const auto& root:strips_)
        for (const auto* strip=root.get();strip;strip=strip->nextTexture.get())
            if (strip->retentionCount!=0) return true;
    return false;
}

void FlailerStripPool::Complete(FlailerStrip& strip)
{
    Require(strip.pool==this && strip.retentionCount,"Flailer strip completion has no retained owner");
    --strip.retentionCount;
    if (!strip.retentionCount && strip.deleteRequested) Release(&strip);
}

void FlailerStripPool::Discard(FlailerObjectChunk& object)
{
    auto* strip=object.strip;
    if (!strip) return;
    Require(strip->pool==this,"Flailer strip belongs to another cache owner");
    if (strip->retentionCount) strip->deleteRequested=true;
    else Release(strip);
    object.strip=nullptr;
}

void FlailerStripPool::Release(FlailerStrip* strip)
{
    const auto found=std::find_if(strips_.begin(),strips_.end(),
        [strip](const auto& owned){return owned.get()==strip;});
    Require(found!=strips_.end(),"Flailer strip cache owner is absent");
    strips_.erase(found);
}

// PAL1416C0..141734 walks every screen even when it currently owns no
// strip: metadata lookup may already have cached its bitmap descriptor.
void FlailerResources::DeleteAllStrips()
{
    // Font atlases are a native lookup cache too. Retaining metadata prevents
    // UAF but must not keep a retired pack selected after resource replacement.
    for (const auto& storage:storage_->fonts) {
        auto& font=storage->font;
        font.texture=nullptr;font.textureStorage.reset();
        font.textureWidth=font.textureHeight=0.0f;
    }
    auto& packages=PackageRoot();
    for (auto* package=packages.next;package!=&packages;package=package->next) {
        auto& screens=package->screenRoot;
        for (auto* screen=screens.next;screen!=&screens;screen=screen->next)
            screen->DeleteAllFlailerStrips();
    }
}

FlailerStripPool& FlailerResources::StripPool() noexcept
{
    return storage_->stripPool;
}

const EditorMarkerTexture* Font::GetTextureInfo(FlailerResources& resources) const
{
    if (texture) return texture;
    auto* registry=resources.Models().resourceRegistry;
    texture=registry ? registry->ResolveTexture(key,false) : resources.FindTexture(key);
    if (!texture) return nullptr;
    Require(texture->authoredRecord && texture->authoredRecordSize>=40,
        "Font atlas has no retained texture dimensions");
    textureStorage=textureMetadataOwner(texture);
    const auto dimension=[](const std::uint8_t* bytes) {
        const auto raw=ReadU16(bytes);std::int16_t signedValue;
        std::memcpy(&signedValue,&raw,2);
        return static_cast<float>(next_power_of_2(static_cast<std::uint32_t>(signedValue)));
    };
    textureWidth=dimension(texture->authoredRecord+36);
    textureHeight=dimension(texture->authoredRecord+38);
    return texture;
}

bool FlailerObjectChunk::DoWeNeedToBuildFlailerStrip(float alphaScale)
{
    if (!strip) return true;
    if (!updateKind && alphaScale==strip->alphaScale) return false;
    DeleteFlailerStrip();
    return true;
}

void FlailerObjectChunk::CreateFlailerStrip(FlailerStripPool& pool,
    const EditorMarkerTexture* texture,float alphaScale)
{
    auto* created=pool.Create(*this,{});
    created->texture=texture;created->alphaScale=alphaScale;
    updateKind=0;
}

void FlailerObjectChunk::DeleteFlailerStrip()
{
    if (strip) strip->pool->Discard(*this);
}

FlailerPoly& FlailerStrip::AddPoly(const EditorMarkerTexture* requestedTexture)
{
    auto* batch=this;
    while (batch->texture!=requestedTexture) {
        if (!batch->nextTexture) {
            batch->nextTexture=std::make_unique<FlailerStrip>();
            batch->nextTexture->pool=pool;
            batch->nextTexture->texture=requestedTexture;
            batch->nextTexture->uiPlacement=uiPlacement;
        }
        batch=batch->nextTexture.get();
    }
    auto& polygon=batch->polygons.emplace_back();
    // PAL10F968 supplies a64x64 square as well as W=0,Q=1. The partial
    // tachometer fan intentionally retains its third default vertex.
    constexpr std::array<std::array<float,2>,4> corners{{{0,0},{1,0},{1,1},{0,1}}};
    for (std::size_t index=0;index<polygon.vertices.size();++index) {
        auto& vertex=polygon.vertices[index];
        vertex.position[0]=64.0f*corners[index][0];
        vertex.position[1]=64.0f*corners[index][1];
        vertex.textureCoordinate[0]=corners[index][0];
        vertex.textureCoordinate[1]=corners[index][1];
        vertex.textureCoordinate[2]=1.0f;
        vertex.color=0x80808080u;
    }
    polygon.rasterState={0x01000100u,0x01000080u,0,0};
    return polygon;
}

FlailerStrip::FlailerStrip()
{
    Require(flailerRuntimeStatistics.liveStrips!=INT32_MAX,"Flailer strip statistics overflow");
    ++flailerRuntimeStatistics.liveStrips;
    flailerRuntimeStatistics.maximumStrips=std::max(flailerRuntimeStatistics.maximumStrips,
        flailerRuntimeStatistics.liveStrips);
}

FlailerStrip::~FlailerStrip()
{
    --flailerRuntimeStatistics.liveStrips;
}

void FlailerStrip::RemovePoly(FlailerPoly& polygon)
{
    for (auto* batch=this;batch;batch=batch->nextTexture.get())
        for (auto it=batch->polygons.begin();it!=batch->polygons.end();++it)
            if (&*it==&polygon) {batch->polygons.erase(it);return;}
    Require(false,"Flailer polygon has no owning texture batch");
}

void FlailerTextChunk::RenderCharacter(const tTTGlyph& glyph,const Font& font,
    const float position[3],const float minimum[2],const float maximum[2],float alphaScale)
{
    Require(strip && font.texture && font.textureWidth!=0.0f && font.textureHeight!=0.0f,
        "Text glyph has no actual strip or atlas binding");
    float left=glyph.atlasPosition[0]/font.textureWidth;
    float top=glyph.atlasPosition[1]/font.textureHeight;
    float right=left+glyph.size[0]/font.textureWidth;
    float bottom=top+glyph.size[1]/font.textureHeight;
    const float halfU=0.5f/font.textureWidth,halfV=0.5f/font.textureHeight;
    const float oneU=1.0f/font.textureWidth,oneV=1.0f/font.textureHeight;
    float width=scaleX*glyph.size[0];
    const float height=scaleY*glyph.size[1];
    auto& polygon=strip->AddPoly(font.texture);
    if(glyph.size[0]>0 && glyph.size[1]>0)
        polygon.glyphSamplingBounds={left+halfU,top+halfV,right-halfU,bottom-halfV};
    left+=halfU;right-=halfU;bottom-=halfV;top+=halfV;
    right+=oneU;bottom+=oneV;width*=scaleX;
    const std::array<std::array<float,2>,4> uv{{{left,top},{right,top},{right,bottom},{left,bottom}}};
    const float rightX=position[0]+width,bottomY=position[1]+height;
    const std::array<std::array<float,3>,4> xyz{{
        {position[0],position[1],position[2]},{rightX,position[1],position[2]},
        {rightX,bottomY,position[2]},{position[0],bottomY,position[2]}}};
    for (std::size_t i=0;i<4;++i) {
        auto& vertex=polygon.vertices[i];
        std::copy(xyz[i].begin(),xyz[i].end(),vertex.position.begin());
        vertex.textureCoordinate[0]=uv[i][0];vertex.textureCoordinate[1]=uv[i][1];
        const auto alpha=static_cast<std::uint8_t>(convertFloatToInt32(
            static_cast<float>(color>>24)*alphaScale));
        vertex.color=(color&0x00ffffffu)|(static_cast<std::uint32_t>(alpha)<<24);
    }
    const auto& state=flailerTextRasterState.rasterBytes;
    polygon.rasterState={static_cast<std::uint32_t>(state[1])|
        (static_cast<std::uint32_t>(state[2])<<8)|(static_cast<std::uint32_t>(state[3])<<16)|
        (static_cast<std::uint32_t>(state[4])<<24),static_cast<std::uint32_t>(state[5])|
        (static_cast<std::uint32_t>(state[0])<<8)|0x01000000u,1,0};
    if (MaybeClipPoly(&polygon,minimum,maximum)) strip->RemovePoly(polygon);
}

float FlailerTextChunk::RenderHelpCharacter(FlailerResources& resources,float x,float y,
    std::uint8_t character,float alphaScale,const char* labelOverride)
{
    Require(IsHelpCharacter(character),"Help glyph is outside the authored mapping");
    const auto device=nativeHelpDevice(resources,*this);
    if(nativePhysicalButtonLabel && device==platform::NativePromptDevice::Xbox) {
        if(character==0x87)character=0x89;
        else if(character==0x89)character=0x87;
    }
    if(const auto* label=labelOverride?labelOverride:nativeHelpLabel(device,character)) {
        const auto* letters=resources.FindFont(hashString("FONT_A"));
        if(letters && letters->GetTextureInfo(resources)) {
            const float width=nativeHelpWidth(device,character,label);
            const float z=static_cast<float>(static_cast<std::int8_t>(authoredFlags));
            const auto alpha=static_cast<std::uint32_t>(std::clamp(alphaScale,0.0f,1.0f)*128.0f)<<24;
            const auto* solid=letters->characters['I'];
            const float u=solid ? (solid->atlasPosition[0]+std::floor(solid->size[0]*0.5f)+0.5f)/letters->textureWidth : 0.0f;
            const float v=solid ? (solid->atlasPosition[1]+std::floor(solid->size[1]*0.5f)+0.5f)/letters->textureHeight : 0.0f;
            const auto quad=[&](const std::array<float,4>& xs,const std::array<float,4>& ys,std::uint32_t rgb) {
                if(!solid)return;
                const auto& lo=flailerTextRasterState.minimum;
                const auto& hi=flailerTextRasterState.maximum;
                const auto [left,right]=std::minmax_element(xs.begin(),xs.end());
                const auto [top,bottom]=std::minmax_element(ys.begin(),ys.end());
                if(*right<=lo[0] || *left>=hi[0] || *bottom<=lo[1] || *top>=hi[1])return;
                auto& polygon=strip->AddPoly(letters->texture);
                for(std::size_t i=0;i<polygon.vertices.size();++i) {
                    polygon.vertices[i].position={
                        std::clamp(xs[i],lo[0],hi[0]),std::clamp(ys[i],lo[1],hi[1]),z,0.0f};
                    polygon.vertices[i].textureCoordinate={u,v,1.0f};
                    polygon.vertices[i].color=alpha|rgb;
                }
                polygon.rasterState[2]=1;
            };
            const auto panel=[&](float left,float top,float right,float bottom,std::uint32_t rgb) {
                quad({left,right,right,left},{top,top,bottom,bottom},rgb);
            };
            constexpr std::uint32_t edge=0x00b8b8b8u,cap=0x00222222u,white=0x00ffffffu;
            const float cx=x+(width-3.0f)*0.5f,cy=y+10.0f;
            const bool xbox=device==platform::NativePromptDevice::Xbox;
            if(xbox && character>=0x86 && character<=0x89) {
                // Xbox face buttons, in A/B/X/Y order. Match the donor's
                // filled circles and keep the glyphs in the original UI font.
                constexpr std::uint32_t faceColors[]{0x0030a030u,0x003030d0u,0x00d86830u,0x0010b0d8u};
                constexpr float radius=9.5f;
                constexpr std::array<float,9> rows{{-1,-0.92388f,-0.707107f,-0.382683f,0,
                    0.382683f,0.707107f,0.92388f,1}};
                const auto rowWidth=[radius](float row) {return radius*std::sqrt(std::max(0.0f,1.0f-row*row));};
                for(std::size_t i=0;i+1<rows.size();++i) {
                    const float a=rowWidth(rows[i]),b=rowWidth(rows[i+1]);
                    quad({cx-a,cx+a,cx+b,cx-b},{cy+radius*rows[i],cy+radius*rows[i],
                        cy+radius*rows[i+1],cy+radius*rows[i+1]},faceColors[character-0x86]);
                }
            } else if(xbox && character>=0x82 && character<=0x85) {
                // Use a physical D-pad icon for directions. LT/RT are
                // reserved for the two trigger inputs.
                const auto cross=[&](float reach,float arm,std::uint32_t rgb) {
                    panel(cx-arm,cy-reach,cx+arm,cy-arm,rgb);
                    panel(cx-reach,cy-arm,cx+reach,cy+arm,rgb);
                    panel(cx-arm,cy+arm,cx+arm,cy+reach,rgb);
                };
                cross(9.5f,3.5f,edge);cross(8.5f,2.5f,cap);
                constexpr float dx[]{0,0,-1,1},dy[]{-1,1,0,0};
                const auto direction=character-0x82u;
                const float a=dx[direction],b=dy[direction],ax=cx+5.0f*a,ay=cy+5.0f*b;
                quad({ax+2.5f*a,ax-1.5f*a-2.0f*b,ax-1.5f*a+2.0f*b,ax-1.5f*a+2.0f*b},
                    {ay+2.5f*b,ay-1.5f*b+2.0f*a,ay-1.5f*b-2.0f*a,ay-1.5f*b-2.0f*a},white);
                return width;
            } else {
                const float top=xbox?y+3.0f:y+1.0f,bottom=xbox?y+17.0f:y+19.0f;
                panel(x,top,x+width-3,bottom,edge);
                panel(x+1,top+1,x+width-4,bottom-1,cap);
                if(xbox && character==0x80) {
                    for(float offset:{-3.0f,0.0f,3.0f})panel(cx-4.0f,cy+offset,cx+4.0f,cy+offset+1.0f,white);
                    return width;
                }
                if(xbox && character==0x81) {
                    panel(cx-5,cy-4,cx+2,cy+2,white);
                    panel(cx-4,cy-3,cx+1,cy+1,cap);
                    panel(cx-1,cy-1,cx+6,cy+5,white);
                    panel(cx,cy,cx+5,cy+4,cap);
                    return width;
                }
            }
            const auto savedX=scaleX,savedY=scaleY;
            const auto savedColor=color;
            scaleX=0.72f;scaleY=0.72f;color=0x80ffffffu;
            float textWidth=0.0f;
            for(const auto* p=label;*p;++p)
                if(const auto* glyph=letters->characters[static_cast<unsigned char>(*p)])textWidth+=glyph->advance*scaleX;
            float position[3]{cx-textWidth*0.5f,y+4.0f,z};
            for(const auto* p=label;*p;++p)
                if(const auto* glyph=letters->characters[static_cast<unsigned char>(*p)]) {
                    if(xbox)position[1]=cy-glyph->size[1]*scaleY*0.5f;
                    RenderCharacter(*glyph,*letters,position,flailerTextRasterState.minimum.data(),
                        flailerTextRasterState.maximum.data(),alphaScale);
                    position[0]+=glyph->advance*scaleX;
                }
            scaleX=savedX;scaleY=savedY;color=savedColor;
            return width;
        }
    }
    // BUTTONSOUT is the actual font key consumed by141E20, not the text font.
    const auto* font=resources.FindFont(hashString("BUTTONSOUT"));
    if (!font || !font->GetTextureInfo(resources)) return 0.0f;
    auto index=static_cast<std::size_t>(character-0x80u);
    if(!nativePhysicalButtonLabel && device==platform::NativePromptDevice::PlayStation) {
        if(index==7)index=9;
        else if(index==9)index=7;
    }
    const auto mapped=flailerTextRasterState.helpGlyphs[index];
    const auto* glyph=font->characters[mapped];
    if (!glyph) return 0.0f;
    const float position[3]{x,y,static_cast<float>(static_cast<std::int8_t>(authoredFlags))};
    RenderCharacter(*glyph,*font,position,flailerTextRasterState.minimum.data(),
        flailerTextRasterState.maximum.data(),alphaScale);
    return glyph->advance;
}

void FlailerRenderManager::AddFlailerStrip(FlailerObjectChunk& object)
{
    auto* strip=object.strip;
    if (!strip) return;
    const auto depth=static_cast<std::int8_t>(object.authoredFlags);
    Require(depth>=0 && static_cast<std::size_t>(depth)<layers.size(),
        "Flailer strip depth is outside the original29 layers");
    auto& destination=layers[static_cast<std::size_t>(depth)];
    std::vector<FlailerStrip*>* source=nullptr;
    std::size_t sourceIndex=0;
    for (auto& layer:layers) {
        const auto it=std::find(layer.begin(),layer.end(),strip);
        if (it!=layer.end()) {source=&layer;sourceIndex=static_cast<std::size_t>(it-layer.begin());break;}
    }
    // Grow the destination before changing either the old queue or its
    // retention; preserve vector's geometric growth on ordinary submissions.
    destination.push_back(strip);
    if (source)source->erase(source->begin()+sourceIndex);
    else {
        try {strip->pool->Retain(*strip);}
        catch (...) {destination.pop_back();throw;}
    }
}

void FlailerRenderManager::SubmitPolygon(FlailerStripPool& pool,const EditorMarkerTexture* texture,
    const FlailerPoly& polygon,std::int32_t depth,EcstasyUiPlacement placement)
{
    Require(texture && depth>=0 && static_cast<std::size_t>(depth)<layers.size(),
        "Immediate frontend polygon has no texture or valid depth");
    FlailerObjectChunk submission;
    submission.authoredFlags=static_cast<std::uint8_t>(depth);
    submission.uiPlacement=placement;
    submission.CreateFlailerStrip(pool,texture,1.0f);
    submission.strip->AddPoly(texture)=polygon;
    AddFlailerStrip(submission);
    submission.DeleteFlailerStrip();
}

void FlailerRenderManager::CompleteSubmissions() noexcept
{
    for (auto& layer:layers) {
        for (auto* strip:layer)strip->pool->Complete(*strip);
        layer.clear();
    }
}

void FlailerRenderManager::RenderTextIntoRectangle(FlailerResources& resources,const char* message,
    FlailerTextChunk& text,const float minimum[2],const float maximum[2],float alphaScale)
{
    if (!text.DoWeNeedToBuildFlailerStrip(alphaScale)) {AddFlailerStrip(text);return;}
    const auto* font=resources.FindFont(text.fontKey);
    Require(font,"Rectangle text has no actual font");
    const auto* atlas=font->GetTextureInfo(resources);
    if (!atlas) return;
    text.CreateFlailerStrip(resources.StripPool(),atlas,alphaScale);
    const float initialX=text.topLeftX;
    float x=initialX,y=text.topLeftY;
    const float available=maximum[0]-initialX;
    const auto delimiter=[](std::int32_t value){return value==' ' || value=='\t' || value==0;};
    const auto* first=reinterpret_cast<const unsigned char*>(message);
    auto* cursor=first;
    const auto device=nativeHelpDevice(resources,text);
    while (*cursor) {
        const auto character=nativeHelpCharacter(device,*font,*cursor++);
        if (IsHelpCharacter(character)) {
            x+=text.RenderHelpCharacter(resources,x,y,character,alphaScale);
            continue;
        }
        const auto* glyph=font->characters[character];
        float position[3]{};
        if (glyph) {
            float kerning=0.0f;
            if (cursor>first+1)kerning=font->GetKerning(static_cast<std::int8_t>(cursor[-2]),glyph)*text.scaleX;
            position[0]=x+kerning;position[1]=y;
            position[2]=static_cast<float>(static_cast<std::int8_t>(text.authoredFlags));
            x+=static_cast<float>(convertFloatToInt32(text.scaleX*(glyph->advance+kerning)));
        }
        bool newline=false;
        if (delimiter(static_cast<std::int8_t>(character))) {
            float width=0.0f;std::size_t count=0;
            for (auto* next=cursor;*next && !delimiter(static_cast<std::int8_t>(*next));++next) {
                Require(++count<256,"Next word exceeds the original256-byte scratch domain");
                if (const auto* nextGlyph=font->characters[*next])width+=nextGlyph->advance;
            }
            width*=text.scaleX;
            if (available<(x+width)-initialX) {
                x=initialX;y+=static_cast<float>(convertFloatToInt32(font->lineHeight));newline=true;
            }
        }
        if (character=='@' && !newline) {
            x=initialX;y+=static_cast<float>(convertFloatToInt32(font->lineHeight));newline=true;
        }
        if (!newline && glyph)text.RenderCharacter(*glyph,*font,position,minimum,maximum,alphaScale);
        else {
            // The PAL loop can walk past its terminator for a final missing
            // glyph/newline. Stop at the C-string boundary in that undefined
            // source domain; defined authored strings keep the same cursor.
            while (*cursor && delimiter(static_cast<std::int8_t>(*cursor)))++cursor;
        }
    }
    AddFlailerStrip(text);
}

void FlailerRenderManager::RenderTextIntoRectangle(FlailerResources& resources,
    std::uint32_t screenKey,std::uint32_t textKey,std::uint32_t clipKey,const char* message,float alphaScale)
{
    auto* clip=static_cast<FlailerBitmapChunk*>(resources.FindObject(screenKey,clipKey,2));
    auto* text=static_cast<FlailerTextChunk*>(resources.FindObject(screenKey,textKey,1));
    if (!clip) return;
    Require(text,"Text clip rectangle has no actual text template");
    const float minimum[2]{clip->topLeftX,clip->topLeftY};
    const float width=static_cast<float>(convertFloatToInt32(resources.ScaledBitmapWidth(*clip)));
    const float height=static_cast<float>(convertFloatToInt32(resources.ScaledBitmapHeight(*clip)));
    const float maximum[2]{clip->topLeftX+width,clip->topLeftY+height};
    text->SetString(message);
    RenderTextIntoRectangle(resources,message,*text,minimum,maximum,alphaScale);
}

// PAL1434A0..143858. The unused first source argument was a needle bitmap;
// the authored tachometer bitmap owns only a borrowed reference to this fan.
void InitializeFlailerTachometer(FlailerResources& resources,FlailerBitmapChunk& bitmap,
    std::array<FlailerTachometerPoint,9>& points)
{
    if (!bitmap.texture) bitmap.ChangeTexture(resources.FindTexture(bitmap.textureKey));
    Require(bitmap.texture && bitmap.texture->authoredRecord && bitmap.texture->authoredRecordSize>=40,
        "Tachometer has no final texture metadata");
    const auto width=static_cast<std::int16_t>(ReadU16(bitmap.texture->authoredRecord+36));
    const auto height=static_cast<std::int16_t>(ReadU16(bitmap.texture->authoredRecord+38));
    Require(width>0 && height>0,"Tachometer texture dimensions are outside their defined domain");
    const auto scaledWidth=convertFloatToInt32(static_cast<float>(width)*bitmap.scaleX);
    const auto scaledHeight=convertFloatToInt32(static_cast<float>(height)*bitmap.scaleY);
    const float left=bitmap.topLeftX,top=bitmap.topLeftY;
    const float right=left+static_cast<float>(scaledWidth),bottom=top+static_cast<float>(scaledHeight);
    const float centerX=left+static_cast<float>(scaledWidth/2);
    const float centerY=top+static_cast<float>(scaledHeight/2);
    const float u=static_cast<float>(width)/static_cast<float>(next_power_of_2(static_cast<uint32_t>(width)));
    const float v=static_cast<float>(height)/static_cast<float>(next_power_of_2(static_cast<uint32_t>(height)));
    const float z=static_cast<float>(static_cast<std::int8_t>(bitmap.authoredFlags));
    points={{{{centerX,centerY,z},{u*0.5f,v*0.5f}},{{centerX,bottom,z},{u*0.5f,v}},
        {{left,bottom,z},{0,v}},{{left,centerY,z},{0,v*0.5f}},{{left,top,z},{0,0}},
        {{centerX,top,z},{u*0.5f,0}},{{right,top,z},{u,0}},
        {{right,centerY,z},{u,v*0.5f}},{{right,bottom,z},{u,v}}}};
    bitmap.tachometerPoints=points.data();
}

// PAL143858..143D50. Retain the final lookup rows and the source partial
// fan's default vertex; a generic pie chart changes its actual geometry.
void RenderFlailerTachometer(FlailerBitmapChunk& bitmap,const FlailerTachometerPoint* points)
{
    const int32_t remaining=convertFloatToInt32(360.0f-static_cast<float>(bitmap.rotation));
    const int32_t sector=remaining/90;
    float remainder=static_cast<float>(remaining%90);
    int32_t split=2;
    if (remainder>=45.0f) {remainder-=45.0f;split=1;}
    const float fraction=1.0f-remainder*0.02222222276031971f;
    if (!points) return;
    Require(bitmap.strip && bitmap.texture && sector>=0 && sector<=4,
        "Tachometer render has no actual strip/texture or a rotation outside its source domain");
    constexpr int32_t indices[5][4]={{0,7,8,1},{0,5,6,7},{0,3,4,5},{0,1,2,3},{0,0,0,0}};
    auto copyPoint=[&](FlailerPolyVertex& vertex,int32_t index) {
        vertex.position[0]=points[index].position[0];
        vertex.position[1]=points[index].position[1];
        vertex.textureCoordinate[0]=points[index].textureCoordinate[0];
        vertex.textureCoordinate[1]=points[index].textureCoordinate[1];
    };
    auto configure=[&](FlailerPoly& polygon,bool complete) {
        const auto& state=flailerTextRasterState.rasterBytes;
        polygon.rasterState={static_cast<uint32_t>(state[1])|(static_cast<uint32_t>(state[2])<<8)|
            (static_cast<uint32_t>(state[3])<<16)|(static_cast<uint32_t>(state[4])<<24),
            static_cast<uint32_t>(state[5])|(static_cast<uint32_t>(state[0])<<8)|0x01000000u,1,0};
        for (std::size_t index=0;index<4;++index) {
            polygon.vertices[index].position[2]=static_cast<float>(static_cast<std::int8_t>(bitmap.authoredFlags));
            polygon.vertices[index].color=complete ? bitmap.colors[index] : 0x80808080u;
        }
    };
    int32_t row=1;
    for (;row<sector;++row) {
        auto& polygon=bitmap.strip->AddPoly(bitmap.texture);
        for (int32_t vertex=0;vertex<4;++vertex) copyPoint(polygon.vertices[vertex],indices[row][vertex]);
        configure(polygon,true);
    }
    auto& polygon=bitmap.strip->AddPoly(bitmap.texture);
    copyPoint(polygon.vertices[0],indices[row][0]);
    const auto& first=points[indices[row][split]];
    const auto& second=points[indices[row][split+1]];
    for (std::size_t axis=0;axis<2;++axis) {
        polygon.vertices[1].position[axis]=first.position[axis]+fraction*(second.position[axis]-first.position[axis]);
        polygon.vertices[1].textureCoordinate[axis]=first.textureCoordinate[axis]+
            fraction*(second.textureCoordinate[axis]-first.textureCoordinate[axis]);
    }
    for (int32_t vertex=split+1;vertex<4;++vertex) copyPoint(polygon.vertices[vertex],indices[row][vertex]);
    configure(polygon,false);
}


// PAL145D90: shared-screen objects precede each visible screen's own list.
void FlailerRenderManager::BeginFrame(FlailerResources& resources,const LanguageManager* language,float textureTime)
{
    for (const auto* screen:visibleScreens)if (screen) {
        if (auto* shared=resources.FindScreen(screen->sharedScreenKey))
            RenderObjectList(resources,shared->objects,language,textureTime);
        RenderObjectList(resources,const_cast<FlailerObjectChunk&>(screen->objects),language,textureTime);
    }
}

// PAL145F30: stable ascending depth sort, followed by the source reverse walk.
std::int32_t FlailerRenderManager::RenderObjectList(FlailerResources& resources,
    FlailerObjectChunk& root,const LanguageManager* language,float textureTime)
{
    bool changed;
    do {
        changed=false;
        for (auto* first=root.next;first!=&root && first->next!=&root;) {
            auto* second=first->next;
            if (SortFlailerObjectFunction(first,second))first=second;
            else {
                second->previous->next=second->next;second->next->previous=second->previous;
                second->previous=first->previous;second->next=first;
                first->previous->next=second;first->previous=second;changed=true;
            }
        }
    } while (changed);
    const auto find=[&](std::uint32_t key)->FlailerObjectChunk* {
        for (auto* object=root.next;object!=&root;object=object->next)if (object->key==key)return object;
        return nullptr;
    };
    const auto dimensions=[&](FlailerObjectChunk& object) {
        std::array<float,2> size{};
        if (object.kind==1) {
            const auto& text=static_cast<const FlailerTextChunk&>(object);
            size={resources.ScaledTextWidth(text,language),resources.ScaledTextHeight(text)};
        } else if (object.kind==2) {
            auto& bitmap=static_cast<FlailerBitmapChunk&>(object);
            size={resources.ScaledBitmapWidth(bitmap),resources.ScaledBitmapHeight(bitmap)};
        }
        return size;
    };
    std::int32_t count=0;
    for (auto* object=root.previous;object!=&root;object=object->previous) {
        if (object->visible!=1)continue;
        ++count;
        if (object->kind==1) {
            auto& text=static_cast<FlailerTextChunk&>(*object);
            auto* clip=text.clipObjectKey && (text.formatOptions&0x10u) ? find(text.clipObjectKey):nullptr;
            if (clip) {
                const auto size=dimensions(*clip);
                const float minimum[2]={clip->topLeftX+4.0f,clip->topLeftY};
                const float maximum[2]={clip->topLeftX+static_cast<float>(convertFloatToInt32(size[0])),
                    clip->topLeftY+static_cast<float>(convertFloatToInt32(size[1]))};
                RenderFlailerText(resources,text,language,minimum,maximum,1.0f);
            } else RenderFlailerText(resources,text,language,nullptr,nullptr,1.0f);
        } else if (object->kind==2) {
            auto& bitmap=static_cast<FlailerBitmapChunk&>(*object);
            if (auto* clip=bitmap.clipObjectKey ? find(bitmap.clipObjectKey):nullptr) {
                float left,top;resources.GetTopLeft(*clip,left,top,language);
                const auto size=dimensions(*clip);SetFlailerTextClipWindow(left,top,left+size[0],top+size[1]);
            }
            RenderFlailerBitmap(resources,bitmap,1.0f,textureTime);
            if (bitmap.clipObjectKey)ResetFlailerTextClipWindow();
        }
    }
    return count;
}

namespace {
void fitRaceHudRow(FlailerResources& resources,FlailerTextChunk& text)
{
    const float margin=1.0f;
    auto& row=*text.hudRow;
    const float rowWidth=static_cast<float>(convertFloatToInt32(resources.ScaledBitmapWidth(row)))-2.0f*margin;
    const float rowHeight=static_cast<float>(convertFloatToInt32(resources.ScaledBitmapHeight(row)))-2.0f*margin;
    if (rowWidth<=0 || rowHeight<=0) return;
    float left=std::numeric_limits<float>::max(),top=left,right=-left,bottom=-left;
    for (auto* batch=text.strip;batch;batch=batch->nextTexture.get()) {
        for (const auto& polygon:batch->polygons) for (const auto& vertex:polygon.vertices) {
            left=std::min(left,vertex.position[0]); top=std::min(top,vertex.position[1]);
            right=std::max(right,vertex.position[0]); bottom=std::max(bottom,vertex.position[1]);
        }
    }
    if (right<=left || bottom<=top) return;
    const float scale=std::min({1.0f,rowWidth/(right-left),rowHeight/(bottom-top)});
    const float width=(right-left)*scale,height=(bottom-top)*scale;
    float origin=left;
    if (text.formatOptions&1u) origin=right-width;
    else if (text.formatOptions&4u) origin=(left+right-width)*0.5f;
    const float rowLeft=row.topLeftX+margin;
    const float x=std::clamp(origin,rowLeft,rowLeft+std::max(0.0f,rowWidth-width));
    const float y=row.topLeftY+margin+(rowHeight-height)*0.5f;
    for (auto* batch=text.strip;batch;batch=batch->nextTexture.get()) {
        for (auto& polygon:batch->polygons) for (auto& vertex:polygon.vertices) {
            vertex.position[0]=x+(vertex.position[0]-left)*scale;
            vertex.position[1]=y+(vertex.position[1]-top)*scale;
        }
    }
}
}

void FlailerRenderManager::RenderFlailerText(FlailerResources& resources,FlailerTextChunk& text,
    const LanguageManager* language,const float* minimum,const float* maximum,float alphaScale)
{
    if (text.DoWeNeedToBuildFlailerStrip(alphaScale)) {
        text.Render(resources,language,text.hudRow?nullptr:minimum,text.hudRow?nullptr:maximum,alphaScale);
        if (text.strip && text.hudRow) fitRaceHudRow(resources,text);
    }
    AddFlailerStrip(text);
}

namespace {
void configureFlailerPolygon(FlailerPoly& polygon,const std::array<std::uint32_t,4>& colors,
    float alphaScale,bool scaleCoordinates)
{
    const auto& state=flailerTextRasterState.rasterBytes;
    polygon.rasterState={static_cast<std::uint32_t>(state[1])|(static_cast<std::uint32_t>(state[2])<<8)|
        (static_cast<std::uint32_t>(state[3])<<16)|(static_cast<std::uint32_t>(state[4])<<24),
        static_cast<std::uint32_t>(state[5])|(static_cast<std::uint32_t>(state[0])<<8)|
        (scaleCoordinates ? 0x01000000u:0u),1u,0u};
    for (std::size_t index=0;index<4;++index) {
        const auto alpha=static_cast<std::uint8_t>(convertFloatToInt32(static_cast<float>(colors[index]>>24)*alphaScale));
        polygon.vertices[index].color=(colors[index]&0x00ffffffu)|(static_cast<std::uint32_t>(alpha)<<24);
    }
}
}

float FlailerTextChunk::GetFixedWidthStringWidth(FlailerResources& resources,const char* first,const char* end) const
{
    const auto* font=resources.FindFont(fontKey);if (!font)return 0.0f;
    float width=0.0f;
    for (auto* cursor=first;cursor<end && *cursor;++cursor)
        if (font->characters[static_cast<unsigned char>(*cursor)])width+=font->fixedAdvance;
    return width;
}

// PAL141F70; source RenderCharacter retains its own distinct width arithmetic.
void FlailerTextChunk::RenderStringFixedWidth(FlailerResources& resources,
    const char* first,const char* end,float alphaScale)
{
    const auto* font=resources.FindFont(fontKey);
    if (!font || !font->GetTextureInfo(resources))return;
    const float width=scaleX*GetFixedWidthStringWidth(resources,first,end);
    float position[3]={topLeftX,topLeftY,static_cast<float>(static_cast<std::int8_t>(authoredFlags))};
    if (formatOptions&1u)position[0]-=width;
    else if (formatOptions&4u)position[0]-=width*0.5f;
    for (auto* cursor=first;cursor<end && *cursor;++cursor) {
        const auto* glyph=font->characters[static_cast<unsigned char>(*cursor)];
        if (!glyph)continue;
        float centered[3]={position[0]+(font->fixedAdvance-glyph->advance)*0.5f,position[1],position[2]};
        RenderCharacter(*glyph,*font,centered,flailerTextRasterState.minimum.data(),
            flailerTextRasterState.maximum.data(),alphaScale);
        position[0]+=scaleX*font->fixedAdvance;
    }
}

void FlailerTextChunk::RenderDarkBackground(FlailerResources& resources,const LanguageManager* language)
{
    const auto* texture=flailerTextRasterState.darkBackground;if (!texture)return;
    const float left=topLeftX-2.0f,top=topLeftY-2.0f;
    float width=resources.ScaledTextWidth(*this,language)+4.0f;
    const float height=resources.ScaledTextHeight(*this)+4.0f;
    if (formatOptions&0x40u)
        width=GetFixedWidthStringWidth(resources,text,text+std::strlen(text))+4.0f;
    const auto depth=static_cast<std::int8_t>(authoredFlags);
    const float z=static_cast<float>(depth<28 ? depth+1:depth);
    auto& polygon=strip->AddPoly(texture);
    const float x[4]={left,left+width,left+width,left},y[4]={top,top,top+height,top+height};
    for (std::size_t index=0;index<4;++index) {
        polygon.vertices[index].position={x[index],y[index],z,0.0f};
        polygon.vertices[index].color=flailerTextRasterState.darkBackgroundColor;
    }
    polygon.rasterState[2]=1;
}

// PAL1422F0, ordinary, fixed-width, translated, wrapped and clipped text.
void FlailerTextChunk::Render(FlailerResources& resources,const LanguageManager* language,
    const float* minimum,const float* maximum,float alphaScale)
{
    const auto* font=resources.FindFont(fontKey);
    if (!font)return;
    const auto* atlas=font->GetTextureInfo(resources);if (!atlas)return;
    CreateFlailerStrip(resources.StripPool(),atlas,alphaScale);
    if (!minimum) {minimum=flailerTextRasterState.minimum.data();maximum=flailerTextRasterState.maximum.data();}
    Require(maximum && text,"Flailer text render has no string or clip bounds");
    const char* first=text;const char* end=text+std::min<std::size_t>(textCapacity,std::strlen(text));
    flailerTextRasterState.lastRenderDistance={0.0f,0.0f};
    if (language && language->textEnabled) {
        if (languageId) {
            if (const auto* translated=language->GetString(static_cast<std::int32_t>(languageId))) {
                first=translated;end=translated+std::strlen(translated);
            }
        } else if (flailerTextRasterState.highlightUntranslated)color=0xffde5454u;
    }
    if (formatOptions&8u)RenderDarkBackground(resources,language);
    if (formatOptions&0x40u) {RenderStringFixedWidth(resources,first,end,alphaScale);return;}
    float x=topLeftX,y=topLeftY;
    const float width=resources.ScaledTextWidth(*this,language);
    if (formatOptions&1u)x-=width;else if (formatOptions&4u)x-=width*0.5f;
    const float initialX=x,initialY=y;bool afterSpace=false;
    const float z=static_cast<float>(static_cast<std::int8_t>(authoredFlags));
    const auto device=nativeHelpDevice(resources,*this);
    for (auto* cursor=first;cursor<end && *cursor;++cursor) {
        if ((formatOptions&0x20u) && afterSpace) {
            const float nextWordWidth=FlailerTextDistanceToNextChar(cursor,' ',font)*scaleX;
            if (flailerTextRasterState.wrapRight<=x+nextWordWidth) {
                x=flailerTextRasterState.wrapLeft;y+=flailerTextRasterState.wrapLineHeight;
            }
            afterSpace=false;
        }
        const auto character=nativeHelpCharacter(device,*font,static_cast<unsigned char>(*cursor));
        if (character==' ')afterSpace=true;
        if (IsHelpCharacter(character)) {x+=RenderHelpCharacter(resources,x,y,character,alphaScale);continue;}
        const auto* glyph=font->characters[character];if (!glyph)continue;
        float kerning=cursor!=first ? font->GetKerning(static_cast<std::int8_t>(cursor[-1]),glyph)*scaleX:0.0f;
        if (std::fabs(kerning)>80.0f)kerning=0.0f;
        const float authoredU=glyph->atlasPosition[0]/font->textureWidth;
        const float authoredV=glyph->atlasPosition[1]/font->textureHeight;
        const float halfU=0.5f/font->textureWidth,halfV=0.5f/font->textureHeight;
        const float left=authoredU+halfU,top=authoredV+halfV;
        const float right=((authoredU+glyph->size[0]/font->textureWidth)-halfU)+1.0f/font->textureWidth;
        const float bottom=((authoredV+glyph->size[1]/font->textureHeight)-halfV)+1.0f/font->textureHeight;
        auto& polygon=strip->AddPoly(atlas);
        if(glyph->size[0]>0 && glyph->size[1]>0)
            polygon.glyphSamplingBounds={left,top,
                authoredU+glyph->size[0]/font->textureWidth-halfU,
                authoredV+glyph->size[1]/font->textureHeight-halfV};
        const float vx[4]={x+kerning,x+scaleX*glyph->size[0]+kerning,x+scaleX*glyph->size[0]+kerning,x+kerning};
        const float vy[4]={y,y,y+scaleY*glyph->size[1],y+scaleY*glyph->size[1]};
        const float u[4]={left,right,right,left},v[4]={top,top,bottom,bottom};
        for (std::size_t index=0;index<4;++index) {
            polygon.vertices[index].position={vx[index],vy[index],z,0.0f};
            polygon.vertices[index].textureCoordinate={u[index],v[index],1.0f};
        }
        if ((formatOptions&0x10u) && MaybeClipPoly(&polygon,minimum,maximum))strip->RemovePoly(polygon);
        else configureFlailerPolygon(polygon,{color,color,color,color},alphaScale,true);
        x+=scaleX*(glyph->advance+kerning);
    }
    flailerTextRasterState.lastRenderDistance={x-initialX,y-initialY};
}

// PAL1428F0. Authored UVs and the source signed integer pixel rounding are
// retained; rotation is the original angle-table eRotateZ operation.
void FlailerBitmapChunk::SetNativeFullscreenMovie(bool enabled)
{
    if(nativeFullscreenMovie!=enabled) {nativeFullscreenMovie=enabled;updateKind=3;}
}

namespace {
bool DrawNativeReplayToolbar(FlailerResources& resources,FlailerBitmapChunk& bitmap,
    const FlailerPoly& base,float width,float height,float alphaScale)
{
    static const auto toolbar=hashString("REPLAYBOTTOM_NEW");
    if(bitmap.textureKey!=toolbar || width<402.0f || height<61.0f)return false;
    const auto device=resources.PromptDevice();
    if(device!=platform::NativePromptDevice::Keyboard && device!=platform::NativePromptDevice::Xbox)return false;
    const auto* font=resources.FindFont(hashString("FONT_A"));
    const auto* atlas=font?font->GetTextureInfo(resources):nullptr;
    if(!atlas)return false;

    struct Button {
        int first,last;
        std::uint8_t glyph;
        const char* key;
        const char* xbox;
    };
    // Measured from REPLAYBOTTOM_NEW. These are the nine baked button
    // cells; the captions and the timeline remain the original artwork.
    constexpr Button buttons[]{
        {16,39,0x8a,"Q","LB"},{62,89,0x84,"Left","Left"},{107,131,0x88,"Space","X"},
        {153,177,0x86,"Enter","A"},{196,221,0x87,"C","B"},{241,265,0x89,"F1","Y"},
        {281,309,0x85,"Right","Right"},{325,354,0x81,"BS","View"},{373,399,0x80,"Esc","Menu"}};
    bitmap.strip->texture=atlas; // The linked background texture is drawn first.
    const auto& start=base.vertices[0];
    const auto& end=base.vertices[2];
    const auto x=[&](float at) {return start.position[0]+(end.position[0]-start.position[0])*at/width;};
    const auto y=[&](float at) {return start.position[1]+(end.position[1]-start.position[1])*at/height;};
    const auto u=[&](float at) {return start.textureCoordinate[0]+(end.textureCoordinate[0]-start.textureCoordinate[0])*at/width;};
    const auto v=[&](float at) {return start.textureCoordinate[1]+(end.textureCoordinate[1]-start.textureCoordinate[1])*at/height;};
    const auto draw=[&](float left,float top,float right,float bottom,float sampleLeft,float sampleRight) {
        auto& quad=bitmap.strip->AddPoly(bitmap.texture);
        quad=base;
        for(std::size_t i=0;i<quad.vertices.size();++i) {
            const bool atRight=i==1 || i==2,atBottom=i>=2;
            auto& vertex=quad.vertices[i];
            vertex.position[0]=x(atRight?right:left);vertex.position[1]=y(atBottom?bottom:top);
            vertex.textureCoordinate[0]=u(atRight?sampleRight:sampleLeft);
            vertex.textureCoordinate[1]=v(atBottom?bottom:top);
        }
        if(MaybeClipPoly(&quad,flailerTextRasterState.minimum.data(),flailerTextRasterState.maximum.data()))
            bitmap.strip->RemovePoly(quad);
    };
    constexpr float top=29.0f,bottom=61.0f;
    draw(0.0f,0.0f,width,top,0.0f,width);
    draw(0.0f,bottom,width,height,0.0f,width);
    float cursor=0.0f;
    FlailerTextChunk prompt;
    prompt.strip=bitmap.strip;prompt.authoredFlags=bitmap.authoredFlags;
    for(const auto& button:buttons) {
        const float left=static_cast<float>(button.first)-2.0f,right=static_cast<float>(button.last)+3.0f;
        draw(cursor,top,left,bottom,cursor,left);
        draw(left,top,right,bottom,left-0.5f,left-0.5f);
        const auto* label=device==platform::NativePromptDevice::Keyboard?button.key:button.xbox;
        prompt.RenderHelpCharacter(resources,x(0.5f*(left+right))-(nativeHelpWidth(device,button.glyph,label)-3.0f)*0.5f,
            y(45.0f)-10.0f,button.glyph,alphaScale,label);
        cursor=right;
    }
    draw(cursor,top,width,bottom,cursor,width);
    prompt.strip=nullptr;
    return true;
}
}

void FlailerRenderManager::RenderSingleFlailerBitmap(FlailerResources& resources,
    FlailerBitmapChunk& bitmap,float alphaScale,float textureTime)
{
    if (!bitmap.DoWeNeedToBuildFlailerStrip(alphaScale)) {AddFlailerStrip(bitmap);return;}
    if (!bitmap.texture) {
        auto* registry=resources.Models().resourceRegistry;
        bitmap.ChangeTexture(registry ? registry->ResolveTexture(bitmap.textureKey,true):resources.FindTexture(bitmap.textureKey));
    }
    bitmap.CreateFlailerStrip(resources.StripPool(),bitmap.texture,alphaScale);
    if (bitmap.authoredMetrics[0]==10) {
        Require(bitmap.tachometerPoints,"Tachometer bitmap has no owning Player fan");
        RenderFlailerTachometer(bitmap,bitmap.tachometerPoints);AddFlailerStrip(bitmap);return;
    }
    Require(bitmap.texture && bitmap.texture->authoredRecord && bitmap.texture->authoredRecordSize>=0x88,
        "Bitmap texture has no actual descriptor");
    const auto* record=bitmap.texture->authoredRecord;
    float scroll[2]{};
    const auto scrollMode=static_cast<std::int8_t>(record[0x79]);
    if (scrollMode) {
        for (std::size_t axis=0;axis<2;++axis)
            scroll[axis]=TextureInfo::GetScroll(textureTime,authoredFloat(record+0x7c+axis*4),
                scrollMode,authoredFloat(record+0x84));
        bitmap.updateKind=1;
    }
    const auto rawWidth=static_cast<std::int16_t>(ReadU16(record+0x24));
    const auto rawHeight=static_cast<std::int16_t>(ReadU16(record+0x26));
    Require(rawWidth>0 && rawHeight>0,"Bitmap texture dimensions are not positive");
    const float width=static_cast<float>(rawWidth),height=static_cast<float>(rawHeight);
    const float paddedWidth=static_cast<float>(next_power_of_2(static_cast<std::uint32_t>(rawWidth)));
    const float paddedHeight=static_cast<float>(next_power_of_2(static_cast<std::uint32_t>(rawHeight)));
    const bool fullMovie=bitmap.nativeFullscreenMovie &&
        (bitmap.texture->key==0xc3f0caefu || bitmap.texture->key==0xf9117c29u);
    const auto drawWidth=convertFloatToInt32(width*bitmap.scaleX);
    const auto drawHeight=convertFloatToInt32(height*bitmap.scaleY);
    const float left=fullMovie?0.0f:bitmap.topLeftX,top=fullMovie?0.0f:bitmap.topLeftY;
    FlailerPoly polygon{};
    for (std::size_t index=0;index<4;++index) {
        const bool right=index==1 || index==2,bottom=index>=2;
        polygon.vertices[index].textureCoordinate={scroll[0]+width*bitmap.textureCoordinates[index][0]/paddedWidth+
            (right?-0.5f:0.5f)/paddedWidth,scroll[1]+height*bitmap.textureCoordinates[index][1]/paddedHeight+
            (bottom?-0.5f:0.5f)/paddedHeight,1.0f};
        polygon.vertices[index].position={left+(right?static_cast<float>(drawWidth):0.0f),
            top+(bottom?static_cast<float>(drawHeight):0.0f),
            static_cast<float>(static_cast<std::int8_t>(bitmap.authoredFlags)),0.0f};
    }
    const bool rotated=!fullMovie && bitmap.authoredMetrics[0]==2 && bitmap.rotation!=0;
    if (rotated) {
        float rotation[16],translation[16],transform[16];eIdentity(rotation);
        const auto angle=static_cast<std::uint16_t>(convertFloatToInt32(static_cast<float>(bitmap.rotation)*65536.0f)/360);
        motionMatrixRotateYaw(rotation,rotation,angle);
        const float center[3]={bitmap.topLeftX+static_cast<float>(drawWidth)*0.5f,
            bitmap.topLeftY+static_cast<float>(drawHeight)*0.5f,0.0f};
        motionMatrixTranslation(translation,center);eMulMatrix(transform,rotation,translation);
        for (std::size_t index=0;index<4;++index) {
            const float local[3]={(index==1 || index==2?0.5f:-0.5f)*static_cast<float>(drawWidth),
                (index>=2?0.5f:-0.5f)*static_cast<float>(drawHeight),0.0f};
            float position[4];eMulVector3(position,transform,local);
            polygon.vertices[index].position[0]=position[0];polygon.vertices[index].position[1]=position[1];
        }
    }
    // Single bitmap leaves ePoly+97's constructor value1 unchanged.
    configureFlailerPolygon(polygon,bitmap.colors,alphaScale,true);
    const bool nativeReplay=!rotated && DrawNativeReplayToolbar(resources,bitmap,polygon,width,height,alphaScale);
    if (!nativeReplay && (fullMovie || rotated ||
        !MaybeClipPoly(&polygon,flailerTextRasterState.minimum.data(),flailerTextRasterState.maximum.data())))
        bitmap.strip->AddPoly(bitmap.texture)=polygon;
    if(fullMovie)bitmap.strip->uiPlacement=EcstasyUiPlacement::FitImage;
    AddFlailerStrip(bitmap);
}

// PAL143078: the two composite images reuse actual scene target textures.
void FlailerRenderManager::RenderFlailerBitmap(FlailerResources& resources,
    FlailerBitmapChunk& bitmap,float alphaScale,float textureTime)
{
    const auto* original=bitmap.texture;
    const bool movieTexture=original && (original->key==0xc3f0caefu || original->key==0xf9117c29u);
    // Retail PAL143148/14314C suppresses movie planes once Close clears
    // MoviePlayer+440. Preserve that source lifetime in native CPU RGBA,
    // even though its conversion uses the source common-zero branch.
    if (movieTexture && bitmapComposite.movie && !bitmapComposite.movie->ValidFrame())return;
    if (!bitmapComposite.enabled || !movieTexture) {
        RenderSingleFlailerBitmap(resources,bitmap,alphaScale,textureTime);return;
    }
    if (!bitmapComposite.targetsReady)return;
    for (const auto* texture:bitmapComposite.textures)if (!texture)return;
    const auto dimension=[](const EditorMarkerTexture& texture) {
        Require(texture.authoredRecord && texture.authoredRecordSize>=40,"Composite target has no descriptor");
        return static_cast<float>(next_power_of_2(static_cast<std::uint32_t>(static_cast<std::int16_t>(ReadU16(texture.authoredRecord+38)))));
    };
    const auto coordinates=bitmap.textureCoordinates;const float scaleX=bitmap.scaleX,scaleY=bitmap.scaleY;
    const auto alphaByte=flailerTextRasterState.rasterBytes[0];
    for (std::size_t pass=0;pass<5;++pass) {
        if (pass<2) {
            const float offset=0.5f/dimension(*bitmapComposite.textures[pass]);
            for (std::size_t corner=0;corner<4;++corner)
                bitmap.textureCoordinates[corner][1]=1.0f-coordinates[corner][1]+(corner<2?offset:-offset);
        }
        if (bitmapComposite.stages[pass]) {
            bitmap.SetScaleX(scaleX*(pass==0?1.0f:2.0f));bitmap.SetScaleY(scaleY*(pass==0?1.0f:2.0f));
            bitmap.ChangeTexture(bitmapComposite.textures[pass]);RenderSingleFlailerBitmap(resources,bitmap,alphaScale,textureTime);
        }
    }
    bitmap.ChangeTexture(original);bitmap.textureCoordinates=coordinates;
    bitmap.SetScaleX(scaleX);bitmap.SetScaleY(scaleY);flailerTextRasterState.rasterBytes[0]=alphaByte;
}

// PAL145950 visits the next texture batch before this batch's polygons.
void FlailerStrip::Render(const CarCinematicScreen& view,EcstasyFrame& frame,std::int32_t layer)
{
    if (nextTexture)nextTexture->Render(view,frame,layer);
    if (texture)for (const auto& polygon:polygons)
        frame.AddPolygon(polygon,*texture,view,layer,1.0f,uiPlacement);
    ++renderedFrames;
}

// PAL145E10 detaches the layer immediately. Native destruction waits for the
// device fence instead of freeing the still-borrowed GPU input in this call.
std::int32_t FlailerRenderManager::RenderLayer(std::int32_t layer,const CarCinematicScreen& view,EcstasyFrame& frame)
{
    Require(layer>=0 && static_cast<std::size_t>(layer)<layers.size(),"Flailer layer is outside the original29 lists");
    auto& pending=layers[static_cast<std::size_t>(layer)];std::int32_t count=0;
    Require(pending.size()<=static_cast<std::size_t>(INT32_MAX),"Flailer layer exceeds its draw-count domain");
    try {
        for (auto* strip:pending) {
            strip->Render(view,frame,layer);frame.RetireStripAfterCompletion(*strip);++count;
        }
    } catch (...) {
        // Only the successfully transferred prefix belongs to the frame.
        // The current strip and suffix remain queued for normal cancellation.
        pending.erase(pending.begin(),pending.begin()+count);throw;
    }
    pending.clear();return count;
}

// The complete PAL145ED0 body is JR RA,NOP. Frame resource retirement belongs
// to RenderLayer and the real GPU completion owner, not this source hook.
void FlailerRenderManager::EndFrame() {}

} // namespace hp2::frontend
