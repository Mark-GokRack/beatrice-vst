// Copyright (c) 2024 Project Beatrice

// TODO(refactor)

#include "vst/editor.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>

#include "vst3sdk/pluginterfaces/vst/vsttypes.h"
#include "vst3sdk/public.sdk/source/vst/utility/stringconvert.h"
#include "vst3sdk/public.sdk/source/vst/vstparameters.h"
#include "vst3sdk/vstgui4/vstgui/lib/cfont.h"
#include "vst3sdk/vstgui4/vstgui/lib/controls/coptionmenu.h"
#include "vst3sdk/vstgui4/vstgui/lib/cviewcontainer.h"
#include "vst3sdk/vstgui4/vstgui/lib/platform/platformfactory.h"
#include "vst3sdk/vstgui4/vstgui/lib/vstguifwd.h"

// Beatrice
#include "common/error.h"
#include "common/parameter_schema.h"
#include "vst/controller.h"
#include "vst/controls.h"

#ifdef BEATRICE_ONLY_FOR_LINTER_DO_NOT_COMPILE_WITH_THIS
#include "vst/metadata.h.in"
#else
#include "metadata.h"  // NOLINT(build/include_subdir)
#endif

namespace beatrice::vst {

using common::ParameterID;
using Steinberg::ViewRect;
using Steinberg::Vst::String128;
using Steinberg::Vst::StringListParameter;
using VSTGUI::CFontDesc;
using VSTGUI::CFrame;
using VSTGUI::COptionMenu;
using VSTGUI::CViewContainer;
using VSTGUI::getPlatformFactory;
using VSTGUI::kBoldFace;
using VSTGUI::kNormalFont;

using std::operator""s;

namespace BitmapFilter = VSTGUI::BitmapFilter;

Editor::Editor(void* const controller)
    : VSTGUIEditor(controller),
      font_(new CFontDesc(kNormalFont->getName(), 14)),
      font_bold_(new CFontDesc(kNormalFont->getName(), 14, kBoldFace)),
      portrait_view_(),
      merge_weight_view_() {
  setRect(ViewRect(0, 0, kWindowWidth, kWindowHeight));
}

Editor::~Editor() {
  font_->forget();
  font_bold_->forget();
}

auto PLUGIN_API Editor::open(void* const parent,
                             const PlatformType& /*platformType*/) -> bool {
  if (frame) {
    return false;
  }
  frame = new CFrame(CRect(0, 0, kWindowWidth, kWindowHeight), this);
  if (!frame) {
    return false;
  }

  // 背景を設定
  frame->setBackgroundColor(kDarkColorScheme.background);

  // ヘッダーを作る
  auto* const header =
      new CViewContainer(CRect(0, 0, kWindowWidth, kHeaderHeight));
  header->setBackgroundColor(kDarkColorScheme.surface_0);
  frame->addView(header);

  // ロゴを表示する
  auto* const logo_view = new CView(CRect(0, 0, 132, 44).offset(34, 7));
  auto* const logo_bmp = new CBitmap("logo.png");
  logo_view->setBackground(logo_bmp);
  header->addView(logo_view);
  logo_bmp->forget();

  // バージョンを表示する
  auto* const version_label = new CTextLabel(
      CRect(0, 0, 200, kHeaderHeight).offset(kWindowWidth - 200 - 17, 0),
      (UTF8String("Ver. ") + FULL_VERSION_STR).data(), nullptr,
      CParamDisplay::kNoFrame);
  version_label->setBackColor(kTransparentCColor);
  version_label->setFont(VSTGUI::kNormalFont);
  version_label->setFontColor(kDarkColorScheme.on_surface);
  version_label->setHoriAlign(CHoriTxtAlign::kRightText);
  header->addView(version_label);

  // フッターを作る
  auto* const footer = new CViewContainer(
      CRect(0, kWindowHeight - kFooterHeight, kWindowWidth, kWindowHeight));
  footer->setBackgroundColor(kDarkColorScheme.surface_0);
  frame->addView(footer);

  auto context = Context();  // オフセット設定
  BeginColumn(context, kColumnWidth, kDarkColorScheme.surface_1);
  BeginGroup(context, u8"General");
  MakeSlider(context, static_cast<ParamID>(ParameterID::kInputGain), 1);
  MakeSlider(context, static_cast<ParamID>(ParameterID::kOutputGain), 1);
  MakeSlider(context, static_cast<ParamID>(ParameterID::kPitchShift), 2);
  MakeSlider(context, static_cast<ParamID>(ParameterID::kAverageSourcePitch),
             2);
  MakeCombobox(context, static_cast<ParamID>(ParameterID::kLock),
               kTransparentCColor, kDarkColorScheme.on_surface);
  MakeSlider(context, static_cast<ParamID>(ParameterID::kIntonationIntensity),
             1);
  MakeSlider(context, static_cast<ParamID>(ParameterID::kPitchCorrection), 1);
  MakeCombobox(context, static_cast<ParamID>(ParameterID::kPitchCorrectionType),
               kTransparentCColor, kDarkColorScheme.on_surface);
  EndGroup(context);
  EndColumn(context);

  BeginColumn(context, kColumnWidth, kDarkColorScheme.surface_2);
  BeginGroup(context, u8"Model");
  MakeFileSelector(context, static_cast<ParamID>(ParameterID::kModel));
  MakeCombobox(context, static_cast<ParamID>(ParameterID::kVoice),
               kDarkColorScheme.primary, kDarkColorScheme.on_primary);
  MakeSlider(context, static_cast<ParamID>(ParameterID::kFormantShift), 2);
  EndGroup(context);
  
  BeginGroup(context, u8"Voice Merge");
  MakeVoiceMergeView(context);
  EndGroup(context);  

  EndColumn(context);

  BeginColumn(context, kModelInfoColumnWidth, kDarkColorScheme.surface_3);
  BeginGroup(context, u8"Information");
  MakePortraitView(context);
  MakeModelVoiceDescription(context);
  EndGroup(context);
  EndColumn(context);

  // 1 要素 1 クラスの方が良かったのか？？？

  if (!frame->open(parent)) {
    return false;
  }

  // frame->open で Attach された後でないと
  // テキストに合わせてテキストボックスの位置が変わらない
  SyncModelDescription();

  return true;
}

void PLUGIN_API Editor::close() {
  if (frame) {
    frame->forget();
    frame = nullptr;
    portraits_.clear();
    portrait_view_ = nullptr;
    merge_weight_view_ = nullptr;
  }
}

// DAW 側から GUI にパラメータ変更を伝える。
// valueChanged からも controller を介して間接的に呼ばれる。
// 引数じゃなくて core から値を取った方が良い？
void Editor::SyncValue(const ParamID param_id,
                       const ParamValue normalized_value) {
  if (!frame || !controls_.contains(param_id)) {
    return;
  }
  auto* const control = controls_.at(param_id);

  // Voice は色々ややこしいので特別扱いする
  if (param_id == static_cast<ParamID>(ParameterID::kVoice)) {
    const auto voice_id =
        Denormalize(std::get<common::ListParameter>(
                        common::kSchema.GetParameter(ParameterID::kVoice)),
                    normalized_value);
    // controller と editor で最大値が異なるため
    // setValueNormalized は正しく動かない
    control->setValue(static_cast<float>(voice_id));
    portrait_view_->setBackground(
        portraits_.at(model_config_->voices[voice_id].portrait.path).get());
    model_voice_description_.SetPortraitDescription(
        model_config_->voices[voice_id].portrait.description);
    model_voice_description_.SetVoiceDescription(
        model_config_->voices[voice_id].description);
    model_voice_description_.SetPortraitDescription(
        model_config_->voices[voice_id].portrait.description);
  } else {
    control->setValueNormalized(static_cast<float>(normalized_value));
  }
  control->setDirty();
}

void Editor::SyncStringValue(const ParamID param_id,
                             const std::u8string& value) {
  if (!frame || !controls_.contains(param_id)) {
    return;
  }
  auto* const control = static_cast<CTextLabel*>(controls_.at(param_id));
  if (param_id == static_cast<ParamID>(ParameterID::kModel)) {
    auto* const model_selector = static_cast<FileSelector*>(control);
    model_selector->SetPath(value);
    SyncModelDescription();
  } else {
    control->setText(reinterpret_cast<const char*>(value.c_str()));
  }
}

// model_selector->getPath() をもとに
// 話者リスト等を更新する
void Editor::SyncModelDescription() {
  auto* const model_selector = static_cast<FileSelector*>(
      controls_.at(static_cast<ParamID>(ParameterID::kModel)));
  auto* const voice_combobox = static_cast<COptionMenu*>(
      controls_.at(static_cast<ParamID>(ParameterID::kVoice)));
  const auto file = model_selector->GetPath();
  model_selector->setText("<unloaded>");
  voice_combobox->removeAllEntry();
  model_voice_description_.SetModelDescription(u8"");
  model_voice_description_.SetVoiceDescription(u8"");
  model_voice_description_.SetPortraitDescription(u8"");
  model_config_ = std::nullopt;
  portraits_.clear();
  if (file.empty()) {
    // 初期状態
    return;
  } else if (!std::filesystem::exists(file) ||
             !std::filesystem::is_regular_file(file)) {
    // ファイルが移動して読み込めない場合の分岐だが、
    // モデルを読み込んだ後に GUI を閉じモデルファイルを移動して
    // 再び GUI を開いた場合などには
    // Processor のみ読み込まれている可能性がある。
    model_selector->setText("<failed to load>");
    model_voice_description_.SetModelDescription(
        u8"Error: The model could not be loaded due to a file move or another "
        u8"issue. Please reload a valid model.");
    return;
  }
  try {
    const auto toml_data = toml::parse(file);
    model_config_ = toml::get<common::ModelConfig>(toml_data);
    if (model_config_->model.VersionInt() == -1) {
      model_voice_description_.SetModelDescription(
          u8"Error: Unknown model version.");
      return;
    }
    model_selector->setText(
        reinterpret_cast<const char*>(model_config_->model.name.c_str()));
    // 話者のリストを読み込む。
    // また、予め portrait を読み込んで、必要に応じてリサイズしておく。
    bool isFirstEmpty = true;
    int voice_counter = 0;
    for (const auto& voice : model_config_->voices) {
      if (voice.name.empty() && voice.description.empty() &&
          voice.portrait.path.empty() && voice.portrait.description.empty()) {
        if( isFirstEmpty ){
          isFirstEmpty = false;
          voice_combobox->addEntry("Voice Merge Mode");
          goto load_portrait_failed;
        }
        break;
      }
      voice_counter++;
      voice_combobox->addEntry(
          reinterpret_cast<const char*>(voice.name.c_str()));
      
      // portrait
      {
        if (portraits_.contains(voice.portrait.path)) {
          goto load_portrait_succeeded;
        }
        const auto portrait_file = file.parent_path() / voice.portrait.path;
        if (!std::filesystem::exists(portrait_file) ||
            !std::filesystem::is_regular_file(portrait_file)) {
          goto load_portrait_failed;
        }
        const auto platform_bitmap = getPlatformFactory().createBitmapFromPath(
            reinterpret_cast<const char*>(portrait_file.u8string().c_str()));
        if (!platform_bitmap) {
          goto load_portrait_failed;
        }
        const auto original_bitmap =
            VSTGUI::owned(new CBitmap(platform_bitmap));
        const auto original_size = original_bitmap->getSize();
        if (original_size.x == kPortraitWidth &&
            original_size.y == kPortraitHeight) {
          portraits_.insert({voice.portrait.path, original_bitmap});
          goto load_portrait_succeeded;
        }
        const auto scale =
            VSTGUI::owned(BitmapFilter::Factory::getInstance().createFilter(
                BitmapFilter::Standard::kScaleBilinear));
        scale->setProperty(BitmapFilter::Standard::Property::kInputBitmap,
                           original_bitmap.get());
        scale->setProperty(BitmapFilter::Standard::Property::kOutputRect,
                           CRect(0, 0, kPortraitWidth, kPortraitHeight));
        if (!scale->run()) {
          goto load_portrait_failed;
        }
        auto* const scaled_bitmap_obj =
            scale->getProperty(BitmapFilter::Standard::Property::kOutputBitmap)
                .getObject();
        auto* const scaled_bitmap = dynamic_cast<CBitmap*>(scaled_bitmap_obj);
        if (!scaled_bitmap) {
          goto load_portrait_failed;
        }
        portraits_.insert({voice.portrait.path, VSTGUI::shared(scaled_bitmap)});
        goto load_portrait_succeeded;
      }
      assert(false);
    load_portrait_failed:
      portraits_.insert({voice.portrait.path, nullptr});
    load_portrait_succeeded: {}
    }
    voice_combobox->setDirty();
    for( auto i = 0; i < common::kMaxNSpeakers; i++ ){
      auto* const slider = static_cast<Slider*>(
          controls_.at(static_cast<ParamID>(
            static_cast<int>( ParameterID::kVoiceMergeWeight ) + i )) );
      auto* const label = static_cast<CTextLabel*>(
          controls_.at(static_cast<ParamID>(
            static_cast<int>(ParameterID::kVoiceMergeLabels) + i )));
      if( i < voice_counter ){
        slider->setVisible(true);
        label->setVisible(true);
        label->setText( reinterpret_cast<const char*>(
          model_config_->voices[i].name.c_str() ));
      }else{
        slider->setVisible(false);
        label->setVisible(false);
        label->setText( "" );
      }
      slider->setDirty();
      label->setDirty();
    }
    const auto voice_id =
        Denormalize(std::get<common::ListParameter>(
                        common::kSchema.GetParameter(ParameterID::kVoice)),
                    controller->getParamNormalized(
                        static_cast<ParamID>(ParameterID::kVoice)));
    const auto& voice = model_config_->voices[voice_id];
    portrait_view_->setBackground(portraits_.at(voice.portrait.path).get());
    portrait_view_->setDirty();
    model_voice_description_.SetModelDescription(
        model_config_->model.description);
    model_voice_description_.SetVoiceDescription(voice.description);
    model_voice_description_.SetPortraitDescription(voice.portrait.description);

    merge_weight_view_->setContainerSize(
      CRect(0, 0, kColumnWidth - 2 * ( kInnerColumnMerginX + kGroupIndentX ), 
        voice_counter * ( kElementHeight + kElementMerginY ) )
    );
    if( voice_id == voice_counter){
      merge_weight_view_->setVisible(true);
    }else{
      merge_weight_view_->setVisible(false);
    }
    merge_weight_view_->setDirty();
    if (auto* const column_view =
            model_voice_description_.model_description_->getParentView()) {
      column_view->setDirty();
    }
  } catch (const std::exception& e) {
    model_selector->setText("<failed to load>");
    model_voice_description_.SetModelDescription(
        u8"Error:\n" +
        std::u8string(e.what(), e.what() + std::strlen(e.what())));
    return;
  }
}

// GUI でパラメータに変更があったときに、DAW に伝える。
// この中で量子化しているが、スライダーの 1 箇所をクリックし続けただけで
// ループしてしまう気がするので、できれば Slider の実装側で量子化したい。
// あとダブルクリックでデフォルトに戻したい。
void Editor::valueChanged(CControl* const pControl) {
  const auto vst_param_id = pControl->getTag();
  const auto param_id = static_cast<ParameterID>(vst_param_id);
  const auto& param = common::kSchema.GetParameter(param_id);
  auto* const controller = static_cast<Controller*>(getController());
  auto& core = controller->core_;
  const auto communicate = [&controller](const int param_id,
                                         const double normalized_value) {
    controller->setParamNormalized(param_id, normalized_value);
    controller->beginEdit(param_id);
    controller->performEdit(param_id, normalized_value);
    controller->endEdit(param_id);
  };
  // 各々の Control でやるべきという感じも
  if (auto* const control = dynamic_cast<Slider*>(pControl)) {
    // communicate 含めて controller の中に処理書いた方が明快？
    const auto* const num_param = std::get_if<common::NumberParameter>(&param);
    assert(num_param);
    auto normalized_value = control->getValueNormalized();
    const auto plain_value = Denormalize(*num_param, normalized_value);
    if (plain_value ==
        std::get<double>(core.parameter_state_.GetValue(param_id))) {
      return;
    }
    normalized_value = static_cast<float>(Normalize(*num_param, plain_value));
    const auto error_code = num_param->ControllerSetValue(core, plain_value);
    assert(error_code == common::ErrorCode::kSuccess);
    communicate(vst_param_id, normalized_value);
  } else if (auto* const control = dynamic_cast<COptionMenu*>(pControl)) {
    const auto* const list_param = std::get_if<common::ListParameter>(&param);
    assert(list_param);
    const auto plain_value = static_cast<int>(control->getValue());
    if( vst_param_id == static_cast<int>(ParameterID::kVoice)){
      if( plain_value == static_cast<int>(control->getMax()) && plain_value < common::kMaxNSpeakers-1 ){
        merge_weight_view_->setVisible(true);
      }else{
        merge_weight_view_->setVisible(false);
      }
    }
    if (plain_value ==
        std::get<int>(core.parameter_state_.GetValue(param_id))) {
      return;
    }
    const auto normalized_value = Normalize(*list_param, plain_value);
    const auto error_code = list_param->ControllerSetValue(core, plain_value);
    if (error_code == common::ErrorCode::kSpeakerIDOutOfRange) {
      // これが表示されることは無いはず
      model_voice_description_.SetVoiceDescription(
          u8"Error: Speaker ID out of range.");
    }
    assert(error_code == common::ErrorCode::kSuccess);
    communicate(vst_param_id, normalized_value);
  } else if (auto* const control = dynamic_cast<FileSelector*>(pControl)) {
    const auto* const str_param = std::get_if<common::StringParameter>(&param);
    assert(str_param);
    const auto file = control->GetPath();
    auto error_code = str_param->ControllerSetValue(core, file.u8string());
    if (error_code == common::ErrorCode::kFileOpenError ||
        error_code == common::ErrorCode::kTOMLSyntaxError) {
      // Controller とは別に Editor::SyncModelDescription でも改めて
      // ファイルを読み込もうとして失敗するので、ここではエラー処理しない
      error_code = common::ErrorCode::kSuccess;
    }
    assert(error_code == common::ErrorCode::kSuccess);
    error_code = controller->SetStringParameter(vst_param_id, file.u8string());
    assert(error_code == common::ErrorCode::kSuccess);
  } else {
    assert(false);
  }

  // 連動するパラメータの処理
  for (const auto& param_id : core.updated_parameters_) {
    const auto vst_param_id = static_cast<ParamID>(param_id);
    const auto& value = core.parameter_state_.GetValue(param_id);
    const auto& param = common::kSchema.GetParameter(param_id);
    if (const auto* const num_param =
            std::get_if<common::NumberParameter>(&param)) {
      const auto normalized_value =
          Normalize(*num_param, std::get<double>(value));
      communicate(vst_param_id, normalized_value);
    } else if (const auto* const list_param =
                   std::get_if<common::ListParameter>(&param)) {
      const auto normalized_value =
          Normalize(*list_param, std::get<int>(value));
      communicate(vst_param_id, normalized_value);
    } else if (std::get_if<common::StringParameter>(&param)) {
      // 現状何かに連動して StringParameter が変化することはない
      assert(false);
    } else {
      assert(false);
    }
  }
  core.updated_parameters_.clear();
}

// auto Editor::notify(CBaseObject* const sender,
//                            const char* const message) -> CMessageResult{
//     return VSTGUIEditor::notify(sender, message);
// }

// 以下は open() からのみ呼ばれるメンバ関数

void Editor::BeginColumn(Context& context, const int width,
                         const CColor& back_color) {
  context.column_width = width;
  context.column_back_color = back_color;
  context.column_start_y = context.y;
  context.column_start_x = context.x;
  context.y = 0;
  context.x = 0;
  context.last_element_mergin = kInnerColumnMerginY;
  context.x += kInnerColumnMerginX;
}

auto Editor::EndColumn(Context& context) -> CView* {
  // const auto bottom =
  //     context.y + std::max(context.last_element_mergin,
  //     kInnerColumnMerginY);
  const auto bottom = kWindowHeight - kFooterHeight;
  auto* const column = new CViewContainer(
      CRect(context.column_start_x, kHeaderHeight,
            context.column_start_x + context.column_width, bottom));
  column->setBackgroundColor(context.column_back_color);
  frame->addView(column);
  for (auto&& element : context.column_elements) {
    column->addView(element);
  }
  context.column_elements.clear();
  context.y = kHeaderHeight;
  context.x = context.column_start_x + context.column_width + kColumnMerginX;
  context.column_start_y = -1;
  context.column_start_x = -1;
  context.last_element_mergin = 0;
  context.first_group = true;
  return column;
}

auto Editor::BeginGroup(Context& context, const std::u8string& name) -> CView* {
  if (!context.first_group) {
    context.last_element_mergin = 20;  // 線を引くとかする？
  }
  context.first_group = false;
  context.y += std::max(context.last_element_mergin, kGroupLabelMerginY);
  auto* const group_label =
      new CTextLabel(CRect(0, 0, context.column_width, kElementHeight)
                         .offset(context.x, context.y),
                     reinterpret_cast<const char*>((u8"⚙ " + name).c_str()),
                     nullptr, CParamDisplay::kNoFrame);
  group_label->setBackColor(kTransparentCColor);
  group_label->setFont(font_bold_);
  group_label->setFontColor(kDarkColorScheme.on_surface);
  group_label->setHoriAlign(CHoriTxtAlign::kLeftText);
  context.column_elements.push_back(group_label);
  context.y += kElementHeight;
  context.x += kGroupIndentX;
  context.last_element_mergin = kGroupLabelMerginY;
  return group_label;
}

void Editor::EndGroup(Context& context) {
   context.x -= kGroupIndentX;
   context.y += kElementMerginY;
}

// NumberParameter 用
auto Editor::MakeSlider(Context& context, const ParamID param_id,
                        const int precision) -> CView* {
  static constexpr auto kHandleWidth = 10;  // 透明の左右の淵を含む
  auto* const param =
      static_cast<LinearParameter*>(controller->getParameterObject(param_id));
  auto* const slider_bmp =
      new MonotoneBitmap(kElementWidth, kElementHeight, kTransparentCColor,
                         kDarkColorScheme.outline);
  auto* const handle_bmp =
      new MonotoneBitmap(kHandleWidth, kElementHeight,
                         kDarkColorScheme.secondary_dim, kTransparentCColor);

  context.y += std::max(context.last_element_mergin, kElementMerginY);

  // 名前
  const auto title_pos = CRect(0, 0, kLabelWidth, kElementHeight)
                             .offset(context.x, context.y);
  const auto title_string =
      Steinberg::Vst::StringConvert::convert(param->getInfo().title);
  auto* const title_control = new CTextLabel(title_pos, title_string.c_str(),
                                             nullptr, CParamDisplay::kNoFrame);
  title_control->setBackColor(kTransparentCColor);
  title_control->setFont(font_);
  title_control->setFontColor(kDarkColorScheme.on_surface);
  title_control->setHoriAlign(CHoriTxtAlign::kCenterText);
  context.column_elements.push_back(title_control);

  const auto slider_offset_x = context.x + kLabelWidth + kElementMerginX;
  auto* const slider_control = new Slider(
      CRect(0, 0, kElementWidth, kElementHeight).offset( slider_offset_x, context.y),
      this, static_cast<int>(param_id), slider_offset_x,
      slider_offset_x + kElementWidth - kHandleWidth, handle_bmp, slider_bmp,
      Steinberg::Vst::StringConvert::convert(param->getInfo().units), font_, precision);
  slider_control->setValueNormalized(
      static_cast<float>(param->getNormalized()));
  context.column_elements.push_back(slider_control);
  slider_bmp->forget();
  handle_bmp->forget();

  controls_.insert({param_id, slider_control});
  context.y += kElementHeight;
  context.last_element_mergin = kElementMerginY;

  return slider_control;
}

// ListParameter 用
auto Editor::MakeCombobox(
    Context& context, const ParamID param_id,
    const CColor& back_color = kTransparentCColor,
    const CColor& font_color = kDarkColorScheme.on_surface) -> CView* {
  auto* const param = static_cast<StringListParameter*>(
      controller->getParameterObject(param_id));
  const auto step_count = param->getInfo().stepCount;

  auto* const bmp = new MonotoneBitmap(kElementWidth, kElementHeight,
                                       back_color, kDarkColorScheme.outline);
  context.y += std::max(context.last_element_mergin, kElementMerginY);


  // 名前
  const auto title_pos = CRect(0, 0, kLabelWidth, kElementHeight)
                             .offset(context.x, context.y);
  const auto title_string =
      Steinberg::Vst::StringConvert::convert(param->getInfo().title);
  auto* const title_control = new CTextLabel(title_pos, title_string.c_str(),
                                             nullptr, CParamDisplay::kNoFrame);
  title_control->setBackColor(kTransparentCColor);
  title_control->setFont(font_);
  title_control->setFontColor(kDarkColorScheme.on_surface);
  title_control->setHoriAlign(CHoriTxtAlign::kCenterText);
  context.column_elements.push_back(title_control);


  const auto pos =
      CRect(0, 0, kElementWidth, kElementHeight).offset(context.x + kLabelWidth + kElementMerginX, context.y);
  auto* const control =
      new COptionMenu(pos, this, static_cast<int>(param_id), bmp);
  bmp->forget();
  for (auto i = 0; i <= step_count; ++i) {
    String128 tmp_string128;
    param->toString(param->toNormalized(i), tmp_string128);
    const auto name = Steinberg::Vst::StringConvert::convert(tmp_string128);
    control->addEntry(name.c_str());
  }
  control->setValueNormalized(
      static_cast<float>(controller->getParamNormalized(param_id)));
  control->setFont(font_);
  control->setFontColor(font_color);
  context.column_elements.push_back(control);
  controls_.insert({param_id, control});

  // ▼ 記号
  const auto arrow_pos =
      CRect(0, 0, kElementHeight, kElementHeight)
          .offset(context.x + kLabelWidth + kElementMerginX + (kElementWidth - kElementHeight), context.y)
          .inset(8, 8);
  auto* const arrow_control =
      new CTextLabel(arrow_pos, reinterpret_cast<const char*>(u8"▼"), nullptr,
                     CParamDisplay::kNoFrame);
  arrow_control->setBackColor(kTransparentCColor);
  auto* const arrow_font =
      new CFontDesc(font_->getName(), font_->getSize() - 6);
  arrow_control->setFont(arrow_font);
  arrow_font->forget();
  arrow_control->setFontColor(font_color);
  arrow_control->setHoriAlign(CHoriTxtAlign::kCenterText);
  // クリックの判定吸われないように、▼ 記号へのマウス操作を無効にする
  arrow_control->setMouseEnabled(false);
  context.column_elements.push_back(arrow_control);

  context.y += kElementHeight;
  context.last_element_mergin = kElementMerginY;

  return control;
}

// StringParameter 用
// クリックされる -> onMouseDown でダイアログが開いてパスを取得
// -> valueChanged から ControllerSetValue が呼ばれる
// -> valueChanged から processor にメッセージ (ファイル名) を送る
// -> notify で processor の mutex を取得、
//    この間 process は無音を出力し、パラメータ変更はキューに詰めとく
// -> モデルを読み込む
// -> valueChanged で連動した他のパラメータの変更が処理される
auto Editor::MakeFileSelector(Context& context,
                              ParamID vst_param_id) -> CView* {
  const auto param_id = static_cast<ParameterID>(vst_param_id);
  const auto param =
      std::get<common::StringParameter>(common::kSchema.GetParameter(param_id));
  auto* const bmp =
      new MonotoneBitmap(kElementWidth, kElementHeight, kTransparentCColor,
                         kDarkColorScheme.outline);
  context.y += std::max(context.last_element_mergin, kElementMerginY);

  // 名前
  const auto title_pos = CRect(0, 0, kLabelWidth, kElementHeight)
                             .offset(context.x, context.y);
  auto* const title_control = new CTextLabel(
      title_pos, reinterpret_cast<const char*>(param.GetName().c_str()),
      nullptr, CParamDisplay::kNoFrame);
  title_control->setBackColor(kTransparentCColor);
  title_control->setFont(font_);
  title_control->setFontColor(kDarkColorScheme.on_surface);
  title_control->setHoriAlign(CHoriTxtAlign::kCenterText);
  context.column_elements.push_back(title_control);

  const auto pos =
      CRect(0, 0, kElementWidth, kElementHeight).offset(context.x + kLabelWidth + kElementMerginX, context.y);
  auto* const control =
      new FileSelector(pos, this, static_cast<int>(vst_param_id), bmp);
  bmp->forget();
  auto* const controller = static_cast<Controller*>(getController());
  control->setBackColor(kTransparentCColor);
  control->setFont(font_);
  control->setFontColor(kDarkColorScheme.on_surface);
  control->setHoriAlign(CHoriTxtAlign::kCenterText);
  control->SetPath(*std::get<std::unique_ptr<std::u8string>>(
      controller->core_.parameter_state_.GetValue(param_id)));
  context.column_elements.push_back(control);
  controls_.insert({vst_param_id, control});


  context.y += kElementHeight;
  context.last_element_mergin = kElementMerginY;

  return control;
}

auto Editor::MakePortraitView(Context& context) -> CView* {
  context.y += std::max(context.last_element_mergin, kElementMerginY);
  portrait_view_ = new CView(CRect(0, 0, kPortraitWidth, kPortraitHeight).offset(context.x, context.y));
  context.column_elements.push_back(portrait_view_);
  context.y += kPortraitHeight;
  context.last_element_mergin = kElementMerginY;
  return portrait_view_;
}

auto Editor::MakeModelVoiceDescription(Context& context) -> CView* {
  context.y += std::max(context.last_element_mergin, 24);
  const auto offset_x = context.x;

  model_voice_description_ = ModelVoiceDescription(
      CRect(context.x, context.y, context.column_width - offset_x,
            kWindowHeight - kFooterHeight),
      VSTGUI::kNormalFontSmall, kElementHeight, kElementMerginY );

  context.column_elements.push_back(
      model_voice_description_.model_description_label_);
  context.column_elements.push_back(
      model_voice_description_.model_description_);
  context.column_elements.push_back(
      model_voice_description_.voice_description_label_);
  context.column_elements.push_back(
      model_voice_description_.voice_description_);
  context.column_elements.push_back(
      model_voice_description_.portrait_description_label_);
  context.column_elements.push_back(
      model_voice_description_.portrait_description_);

  return nullptr;
}

auto Editor::MakeVoiceMergeView(Context& context) -> CView* {
  context.y += std::max(context.last_element_mergin, 24);
  //const auto button_width = ( kColumnWidth - 2 * ( kInnerColumnMerginX + kGroupIndentX ) - kElementMerginX ) / 2;

  const auto size =  CRect(0, 0,
          kColumnWidth - 2 * ( kInnerColumnMerginX + kGroupIndentX ),
          kWindowHeight- kFooterHeight - kHeaderHeight - context.y ).offset( context.x, context.y );
  const auto container_size = CRect(0, 0,
          size.getWidth(), ( kElementHeight + kElementMerginY ) * common::kMaxNSpeakers );
  merge_weight_view_ = new VSTGUI::CScrollView(
    size, container_size,
    VSTGUI::CScrollView::kVerticalScrollbar | VSTGUI::CScrollView::kDontDrawFrame
    | VSTGUI::CScrollView::kOverlayScrollbars | VSTGUI::CScrollView::kAutoHideScrollbars
  );
  merge_weight_view_->setAutosizeFlags( VSTGUI::kAutosizeRow | VSTGUI::kAutosizeBottom );
  merge_weight_view_->setBackgroundColor(kTransparentCColor);
  
  static constexpr auto kHandleWidth = 10;  // 透明の左右の淵を含む
  auto* const slider_bmp =
      new MonotoneBitmap(kElementWidth, kElementHeight, kTransparentCColor,
                         kDarkColorScheme.outline);
  auto* const handle_bmp =
      new MonotoneBitmap(kHandleWidth, kElementHeight,
                         kDarkColorScheme.secondary_dim, kTransparentCColor);

  for( auto i = 0; i < common::kMaxNSpeakers; i++ ){
    auto const vst_param_id = static_cast<int>(ParameterID::kVoiceMergeLabels) + i;
    auto const param_id = static_cast<ParameterID>(vst_param_id);
    const auto param =
        std::get<common::StringParameter>(common::kSchema.GetParameter(param_id));
    const auto label_pos = CRect(0, 0, kLabelWidth, kElementHeight)
                              .offset(0, i * ( kElementHeight + kElementMerginY ) );
    const auto label_string = reinterpret_cast<const char*>( param.GetDefaultValue().c_str() );
    auto* const label_control = new CTextLabel(label_pos, label_string,
                                              nullptr, CParamDisplay::kNoFrame);
    label_control->setTag( static_cast<int>(param_id ));
    label_control->setBackColor(kTransparentCColor);
    label_control->setFont(font_);
    label_control->setFontColor(kDarkColorScheme.on_surface);
    label_control->setHoriAlign(CHoriTxtAlign::kCenterText);
    label_control->setVisible(false);

    merge_weight_view_->addView( label_control );
    controls_.insert({vst_param_id, label_control});
  }
  for( auto i = 0; i < common::kMaxNSpeakers; i++ ){
    auto const vst_param_id = static_cast<int>(ParameterID::kVoiceMergeWeight) + i;
    auto const param_id = static_cast<ParamID>(vst_param_id);
    auto* const param =
        static_cast<LinearParameter*>(controller->getParameterObject(param_id));
    const auto slider_offset_x = kLabelWidth + kElementMerginX;
    const auto slider_width = kElementWidth - merge_weight_view_->getScrollbarWidth();
    auto* const slider_control = new Slider(
        CRect(0, 0, kElementWidth - merge_weight_view_->getScrollbarWidth(), kElementHeight)
        .offset( slider_offset_x, i * ( kElementHeight + kElementMerginY ) ),
        this, static_cast<int>(param_id), slider_offset_x,
        slider_offset_x + slider_width - kHandleWidth, handle_bmp, slider_bmp,
        Steinberg::Vst::StringConvert::convert(param->getInfo().units), font_, 2);
    slider_control->setValueNormalized(
        static_cast<float>(param->getNormalized()));
    slider_control->setVisible(false);

    merge_weight_view_->addView( slider_control );
    controls_.insert({vst_param_id, slider_control});
  }

  slider_bmp->forget();
  handle_bmp->forget();

  merge_weight_view_->setVisible(false);

  context.column_elements.push_back( merge_weight_view_ );
  return merge_weight_view_;
}




}  // namespace beatrice::vst
