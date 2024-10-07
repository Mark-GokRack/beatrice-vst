// Copyright (c) 2024 Project Beatrice

// TODO(refactor)

#include "vst/editor.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <memory>

#include "pluginterfaces/vst/vsttypes.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "vst3sdk/public.sdk/source/vst/utility/stringconvert.h"
#include "vst3sdk/vstgui4/vstgui/lib/cfont.h"
#include "vst3sdk/vstgui4/vstgui/lib/controls/coptionmenu.h"
#include "vst3sdk/vstgui4/vstgui/lib/cviewcontainer.h"
#include "vst3sdk/vstgui4/vstgui/lib/platform/platformfactory.h"
#include "vst3sdk/vstgui4/vstgui/lib/vstguifwd.h"

// Beatrice
#include "vst/controller.h"
#include "vst/controls.h"

namespace beatrice::vst {

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
namespace BitmapFilter = VSTGUI::BitmapFilter;

Editor::Editor(void* const controller)
    : VSTGUIEditor(controller),
      font_(new CFontDesc(kNormalFont->getName(), 14)),
      font_bold_(new CFontDesc(kNormalFont->getName(), 14, kBoldFace)),
      portrait_view_(),
      portrait_description_() {
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
  BeginColumn(context, 399, kDarkColorScheme.surface_1);
  BeginGroup(context, u8"General");
  MakeSlider(context, 2000, 1);  // Gain
  MakeSlider(context, 2001, 1);  // Gain
  MakeSlider(context, 1007, 2);  // PitchShift
  MakeSlider(context, 1008, 2);  // AverageSourcePitch
  MakeCombobox(context, 1009, kTransparentCColor,
               kDarkColorScheme.on_surface);  // Lock
  EndGroup(context);
  EndColumn(context);

  BeginColumn(context, 399, kDarkColorScheme.surface_2);
  BeginGroup(context, u8"Model");
  MakeFileSelector(context, 1);  // Model
  MakeCombobox(context, 1000, kDarkColorScheme.primary,
               kDarkColorScheme.on_primary);  // Voice
  MakeSlider(context, 1006, 2);               // Formant
  MakeModelVoiceDescription(context);
  EndGroup(context);
  EndColumn(context);

  BeginColumn(context, 480, kDarkColorScheme.surface_3);
  MakePortraitView(context);
  MakePortraitDescription(context);

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
    portrait_description_ = nullptr;
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
  if (param_id == 1000) {
    const auto voice_id = Denormalize(
        std::get<common::ListParameter>(common::kSchema.GetParameter(1, 0)),
        normalized_value);
    // controller と editor で最大値が異なるため
    // setValueNormalized は正しく動かない
    control->setValue(static_cast<float>(voice_id));
    portrait_view_->setBackground(
        portraits_.at(model_config_->voices[voice_id].portrait.path).get());
    portrait_description_->setText(reinterpret_cast<const char*>(
        model_config_->voices[voice_id].portrait.description.c_str()));
    model_voice_description_.SetVoiceDescription(
        model_config_->voices[voice_id].description);
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
  if (param_id == 1) {
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
  auto* const model_selector = static_cast<FileSelector*>(controls_.at(1));
  auto* const voice_combobox = static_cast<COptionMenu*>(controls_.at(1000));
  const auto file = model_selector->GetPath();
  model_selector->setText("<unloaded>");
  voice_combobox->removeAllEntry();
  model_voice_description_.SetModelDescription(u8"");
  model_voice_description_.SetVoiceDescription(u8"");
  model_config_ = std::nullopt;
  portraits_.clear();
  if (!std::filesystem::exists(file) ||
      !std::filesystem::is_regular_file(file)) {
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
    for (const auto& voice : model_config_->voices) {
      if (voice.name.empty() && voice.description.empty() &&
          voice.portrait.path.empty() && voice.portrait.description.empty()) {
        break;
      }
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
        static constexpr auto kPortraitSize = 480;
        if (original_size.x == kPortraitSize &&
            original_size.y == kPortraitSize) {
          portraits_.insert({voice.portrait.path, original_bitmap});
          goto load_portrait_succeeded;
        }
        const auto scale =
            VSTGUI::owned(BitmapFilter::Factory::getInstance().createFilter(
                BitmapFilter::Standard::kScaleBilinear));
        scale->setProperty(BitmapFilter::Standard::Property::kInputBitmap,
                           original_bitmap.get());
        scale->setProperty(BitmapFilter::Standard::Property::kOutputRect,
                           CRect(0, 0, kPortraitSize, kPortraitSize));
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
    const auto voice_id = Denormalize(
        std::get<common::ListParameter>(common::kSchema.GetParameter(1, 0)),
        controller->getParamNormalized(1000));
    const auto& voice = model_config_->voices[voice_id];
    portrait_view_->setBackground(portraits_.at(voice.portrait.path).get());
    portrait_view_->setDirty();
    portrait_description_->setText(
        reinterpret_cast<const char*>(voice.portrait.description.c_str()));
    portrait_description_->setDirty();
    model_voice_description_.SetModelDescription(
        model_config_->model.description);
    model_voice_description_.SetVoiceDescription(voice.description);
    if (auto* const column_view =
            model_voice_description_.model_description_->getParentView()) {
      column_view->setDirty();
    }
  } catch (const std::exception& e) {
    model_voice_description_.SetModelDescription(
        u8"Error: Failed to load model.\n" +
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
  const auto group_id = vst_param_id / kParamsPerGroup;
  const auto param_id = vst_param_id % kParamsPerGroup;
  const auto& param = common::kSchema.GetParameter(group_id, param_id);
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
        std::get<double>(core.parameter_state_.GetValue(group_id, param_id))) {
      return;
    }
    normalized_value = static_cast<float>(Normalize(*num_param, plain_value));
    num_param->ControllerSetValue(core, plain_value);
    communicate(vst_param_id, normalized_value);
  } else if (auto* const control = dynamic_cast<COptionMenu*>(pControl)) {
    const auto* const list_param = std::get_if<common::ListParameter>(&param);
    assert(list_param);
    const auto plain_value = static_cast<int>(control->getValue());
    if (plain_value ==
        std::get<int>(core.parameter_state_.GetValue(group_id, param_id))) {
      return;
    }
    const auto normalized_value = Normalize(*list_param, plain_value);
    list_param->ControllerSetValue(core, plain_value);
    communicate(vst_param_id, normalized_value);
  } else if (auto* const control = dynamic_cast<FileSelector*>(pControl)) {
    const auto* const str_param = std::get_if<common::StringParameter>(&param);
    assert(str_param);
    const auto file = control->GetPath();
    str_param->ControllerSetValue(core, file.u8string());
    controller->SetStringParameter(vst_param_id, file.u8string());
  } else {
    assert(false);
  }

  // 連動するパラメータの処理
  for (const auto& [group_id, param_id] : core.updated_parameters_) {
    const auto vst_param_id = kParamsPerGroup * group_id + param_id;
    const auto& value = core.parameter_state_.GetValue(group_id, param_id);
    const auto& param = common::kSchema.GetParameter(group_id, param_id);
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

void Editor::EndGroup(Context& context) { context.x -= kGroupIndentX; }

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
  auto* const slider_control = new Slider(
      CRect(0, 0, kElementWidth, kElementHeight).offset(context.x, context.y),
      this, static_cast<int>(param_id), context.x,
      context.x + kElementWidth - kHandleWidth, handle_bmp, slider_bmp,
      VST3::StringConvert::convert(param->getInfo().units), font_, precision);
  slider_control->setValueNormalized(
      static_cast<float>(param->getNormalized()));
  context.column_elements.push_back(slider_control);
  slider_bmp->forget();
  handle_bmp->forget();

  controls_.insert({param_id, slider_control});

  // 名前
  const auto title_pos = CRect(0, 0, kElementWidth, kElementHeight)
                             .offset(context.x + kElementWidth + 8, context.y);
  const auto title_string =
      VST3::StringConvert::convert(param->getInfo().title);
  auto* const title_control = new CTextLabel(title_pos, title_string.c_str(),
                                             nullptr, CParamDisplay::kNoFrame);
  title_control->setBackColor(kTransparentCColor);
  title_control->setFont(font_);
  title_control->setFontColor(kDarkColorScheme.on_surface);
  title_control->setHoriAlign(CHoriTxtAlign::kLeftText);
  context.column_elements.push_back(title_control);

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
  const auto pos =
      CRect(0, 0, kElementWidth, kElementHeight).offset(context.x, context.y);
  auto* const control =
      new COptionMenu(pos, this, static_cast<int>(param_id), bmp);
  bmp->forget();
  for (auto i = 0; i <= step_count; ++i) {
    String128 tmp_string128;
    param->toString(param->toNormalized(i), tmp_string128);
    const auto name = VST3::StringConvert::convert(tmp_string128);
    control->addEntry(name.c_str());
  }
  control->setValueNormalized(
      static_cast<float>(controller->getParamNormalized(param_id)));
  control->setFont(font_);
  control->setFontColor(font_color);
  context.column_elements.push_back(control);
  controls_.insert({param_id, control});

  // ▼ 記号
  // TODO(bug): クリックの判定吸われるのをなんとかする
  const auto arrow_pos =
      CRect(0, 0, kElementHeight, kElementHeight)
          .offset(context.x + (kElementWidth - kElementHeight), context.y)
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
  context.column_elements.push_back(arrow_control);

  // 名前
  const auto title_pos = CRect(0, 0, kElementWidth, kElementHeight)
                             .offset(context.x + kElementWidth + 8, context.y);
  const auto title_string =
      VST3::StringConvert::convert(param->getInfo().title);
  auto* const title_control = new CTextLabel(title_pos, title_string.c_str(),
                                             nullptr, CParamDisplay::kNoFrame);
  title_control->setBackColor(kTransparentCColor);
  title_control->setFont(font_);
  title_control->setFontColor(kDarkColorScheme.on_surface);
  title_control->setHoriAlign(CHoriTxtAlign::kLeftText);
  context.column_elements.push_back(title_control);

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
  const auto group_id = static_cast<int>(vst_param_id) / kParamsPerGroup;
  const auto param_id = static_cast<int>(vst_param_id) % kParamsPerGroup;
  const auto param = std::get<common::StringParameter>(
      common::kSchema.GetParameter(group_id, param_id));
  auto* const bmp =
      new MonotoneBitmap(kElementWidth, kElementHeight, kTransparentCColor,
                         kDarkColorScheme.outline);
  context.y += std::max(context.last_element_mergin, kElementMerginY);
  const auto pos =
      CRect(0, 0, kElementWidth, kElementHeight).offset(context.x, context.y);
  auto* const control =
      new FileSelector(pos, this, static_cast<int>(vst_param_id), bmp);
  bmp->forget();
  auto* const controller = static_cast<Controller*>(getController());
  control->setBackColor(kTransparentCColor);
  control->setFont(font_);
  control->setFontColor(kDarkColorScheme.on_surface);
  control->setHoriAlign(CHoriTxtAlign::kCenterText);
  control->SetPath(*std::get<std::unique_ptr<std::u8string>>(
      controller->core_.parameter_state_.GetValue(group_id, param_id)));
  context.column_elements.push_back(control);
  controls_.insert({vst_param_id, control});

  // 名前
  const auto title_pos = CRect(0, 0, kElementWidth, kElementHeight)
                             .offset(context.x + kElementWidth + 8, context.y);
  auto* const title_control = new CTextLabel(
      title_pos, reinterpret_cast<const char*>(param.GetName().c_str()),
      nullptr, CParamDisplay::kNoFrame);
  title_control->setBackColor(kTransparentCColor);
  title_control->setFont(font_);
  title_control->setFontColor(kDarkColorScheme.on_surface);
  title_control->setHoriAlign(CHoriTxtAlign::kLeftText);
  context.column_elements.push_back(title_control);

  context.y += kElementHeight;
  context.last_element_mergin = kElementMerginY;

  return control;
}

auto Editor::MakePortraitView(Context& context) -> CView* {
  constexpr auto kSiz = 480;
  portrait_view_ = new CView(CRect(0, 0, kSiz, kSiz));
  context.column_elements.push_back(portrait_view_);
  context.y += kSiz;
  context.last_element_mergin = kElementMerginY;
  return portrait_view_;
}

auto Editor::MakeModelVoiceDescription(Context& context) -> CView* {
  context.y += std::max(context.last_element_mergin, 24);
  const auto offset_x = context.x;

  model_voice_description_ = ModelVoiceDescription(
      CRect(context.x, context.y, context.column_width - offset_x,
            kWindowHeight - kFooterHeight),
      VSTGUI::kNormalFont, kElementHeight, kElementMerginY + 4);

  context.column_elements.push_back(
      model_voice_description_.model_description_label_);
  context.column_elements.push_back(
      model_voice_description_.model_description_);
  context.column_elements.push_back(
      model_voice_description_.voice_description_label_);
  context.column_elements.push_back(
      model_voice_description_.voice_description_);

  return nullptr;
}

auto Editor::MakePortraitDescription(Context& context) -> CView* {
  context.y += std::max(context.last_element_mergin, kElementMerginY);
  const auto offset_x = context.x;
  auto* const description = new CMultiLineTextLabel(
      CRect(context.x, context.y, context.column_width - offset_x,
            kWindowHeight - kFooterHeight));
  description->setFont(font_);
  description->setFontColor(kDarkColorScheme.on_surface);
  description->setBackColor(kTransparentCColor);
  description->setLineLayout(CMultiLineTextLabel::LineLayout::wrap);
  description->setStyle(CParamDisplay::kNoFrame);
  description->setHoriAlign(CHoriTxtAlign::kLeftText);
  portrait_description_ = description;

  context.column_elements.push_back(description);
  context.y = kWindowHeight - kFooterHeight;
  context.last_element_mergin = kElementMerginY;
  return description;
}

}  // namespace beatrice::vst