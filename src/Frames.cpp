#include "AudibleInstruments.hpp"
#include <string.h>
#include "frames/keyframer.h"
#include "frames/poly_lfo.h"
#include "dsp/digital.hpp"


struct Frames : Module {
	enum ParamIds {
		GAIN1_PARAM,
		GAIN2_PARAM,
		GAIN3_PARAM,
		GAIN4_PARAM,
		ADD_PARAM,
		DEL_PARAM,
		FRAME_PARAM,
		MODULATION_PARAM,
		OFFSET_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		ALL_INPUT,
		IN1_INPUT,
		IN2_INPUT,
		IN3_INPUT,
		IN4_INPUT,
		FRAME_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		MIX_OUTPUT,
		OUT1_OUTPUT,
		OUT2_OUTPUT,
		OUT3_OUTPUT,
		OUT4_OUTPUT,
		FRAME_STEP_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		GAIN1_LIGHT,
		EDIT_LIGHT = GAIN1_LIGHT + 4,
		FRAME_LIGHT,
		NUM_LIGHTS = FRAME_LIGHT + 3
	};

	frames::Keyframer keyframer;
	frames::PolyLfo poly_lfo;
	bool poly_lfo_mode = false;
	uint16_t lastControls[4] = {};

	SchmittTrigger addTrigger;
	SchmittTrigger delTrigger;

	Frames();
	void step() override;

	json_t *toJson() override {
		json_t *rootJ = json_object();
		json_object_set_new(rootJ, "polyLfo", json_boolean(poly_lfo_mode));

		json_t *keyframesJ = json_array();
		for (int i = 0; i < keyframer.num_keyframes(); i++) {
			json_t *keyframeJ = json_array();
			frames::Keyframe *keyframe = keyframer.mutable_keyframe(i);
			json_array_append_new(keyframeJ, json_integer(keyframe->timestamp));
			for (int k = 0; k < 4; k++) {
				json_array_append_new(keyframeJ, json_integer(keyframe->values[k]));
			}
			json_array_append_new(keyframesJ, keyframeJ);
		}
		json_object_set_new(rootJ, "keyframes", keyframesJ);

		json_t *channelsJ = json_array();
		for (int i = 0; i < 4; i++) {
			json_t *channelJ = json_object();
			json_object_set_new(channelJ, "curve", json_integer((int) keyframer.mutable_settings(i)->easing_curve));
			json_object_set_new(channelJ, "response", json_integer(keyframer.mutable_settings(i)->response));
			json_array_append_new(channelsJ, channelJ);
		}
		json_object_set_new(rootJ, "channels", channelsJ);

		return rootJ;
	}

	void fromJson(json_t *rootJ) override {
		json_t *polyLfoJ = json_object_get(rootJ, "polyLfo");
		if (polyLfoJ)
			poly_lfo_mode = json_boolean_value(polyLfoJ);

		json_t *keyframesJ = json_object_get(rootJ, "keyframes");
		if (keyframesJ) {
			json_t *keyframeJ;
			size_t i;
			json_array_foreach(keyframesJ, i, keyframeJ) {
				uint16_t timestamp = json_integer_value(json_array_get(keyframeJ, 0));
				uint16_t values[4];
				for (int k = 0; k < 4; k++) {
					values[k] = json_integer_value(json_array_get(keyframeJ, k + 1));
				}
				keyframer.AddKeyframe(timestamp, values);
			}
		}

		json_t *channelsJ = json_object_get(rootJ, "channels");
		if (channelsJ) {
			for (int i = 0; i < 4; i++) {
				json_t *channelJ = json_array_get(channelsJ, i);
				if (channelJ) {
					json_t *curveJ = json_object_get(channelJ, "curve");
					if (curveJ)
						keyframer.mutable_settings(i)->easing_curve = (frames::EasingCurve) json_integer_value(curveJ);
					json_t *responseJ = json_object_get(channelJ, "response");
					if (responseJ)
						keyframer.mutable_settings(i)->response = json_integer_value(responseJ);
				}
			}
		}
	}

	void reset() override {
		poly_lfo_mode = false;
		keyframer.Clear();
		for (int i = 0; i < 4; i++) {
			keyframer.mutable_settings(i)->easing_curve = frames::EASING_CURVE_LINEAR;
			keyframer.mutable_settings(i)->response = 0;
		}
	}
	void randomize() override {
		// TODO
		// Maybe something useful should go in here?
	}
};


Frames::Frames() : Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS) {
	memset(&keyframer, 0, sizeof(keyframer));
	keyframer.Init();
	memset(&poly_lfo, 0, sizeof(poly_lfo));
	poly_lfo.Init();

	reset();
}


void Frames::step() {
	// Set gain and timestamp knobs
	uint16_t controls[4];
	for (int i = 0; i < 4; i++) {
		controls[i] = params[GAIN1_PARAM + i].value * 65535.0;
	}

	int32_t timestamp = params[FRAME_PARAM].value * 65535.0;
	int32_t timestampMod = timestamp + params[MODULATION_PARAM].value * inputs[FRAME_INPUT].value / 10.0 * 65535.0;
	timestamp = clampi(timestamp, 0, 65535);
	timestampMod = clampi(timestampMod, 0, 65535);
	int16_t nearestIndex = -1;
	if (!poly_lfo_mode) {
		nearestIndex = keyframer.FindNearestKeyframe(timestamp, 2048);
	}

	// Render, handle buttons
	if (poly_lfo_mode) {
		if (controls[0] != lastControls[0])
			poly_lfo.set_shape(controls[0]);
		if (controls[1] != lastControls[1])
			poly_lfo.set_shape_spread(controls[1]);
		if (controls[2] != lastControls[2])
			poly_lfo.set_spread(controls[2]);
		if (controls[3] != lastControls[3])
			poly_lfo.set_coupling(controls[3]);
		poly_lfo.Render(timestampMod);
	}
	else {
		for (int i = 0; i < 4; i++) {
			if (controls[i] != lastControls[i]) {
				// Update recently moved control
				if (keyframer.num_keyframes() == 0) {
					keyframer.set_immediate(i, controls[i]);
				}
				if (nearestIndex >= 0) {
					frames::Keyframe *nearestKeyframe = keyframer.mutable_keyframe(nearestIndex);
					nearestKeyframe->values[i] = controls[i];
				}
			}
		}

		if (addTrigger.process(params[ADD_PARAM].value)) {
			if (nearestIndex < 0) {
				keyframer.AddKeyframe(timestamp, controls);
			}
		}
		if (delTrigger.process(params[DEL_PARAM].value)) {
			if (nearestIndex >= 0) {
				int32_t nearestTimestamp = keyframer.keyframe(nearestIndex).timestamp;
				keyframer.RemoveKeyframe(nearestTimestamp);
			}
		}
		keyframer.Evaluate(timestampMod);
	}

	// Get gains
	float gains[4];
	for (int i = 0; i < 4; i++) {
		if (poly_lfo_mode) {
			// gains[i] = poly_lfo.level(i) / 255.0;
			gains[i] = poly_lfo.level16(i) / 65535.0;
		}
		else {
			float lin = keyframer.level(i) / 65535.0;
			gains[i] = lin;
		}
		// Simulate SSM2164
		if (keyframer.mutable_settings(i)->response > 0) {
			const float expBase = 200.0;
			float expGain = rescalef(powf(expBase, gains[i]), 1.0, expBase, 0.0, 1.0);
			gains[i] = crossf(gains[i], expGain, keyframer.mutable_settings(i)->response / 255.0);
		}
	}

	// Update last controls
	for (int i = 0; i < 4; i++) {
		lastControls[i] = controls[i];
	}

	// Get inputs
	float all = ((int)params[OFFSET_PARAM].value == 1) ? 10.0 : 0.0;
	if (inputs[ALL_INPUT].active) {
		all = inputs[ALL_INPUT].value;
	}

	float ins[4];
	for (int i = 0; i < 4; i++) {
		ins[i] = inputs[IN1_INPUT + i].normalize(all) * gains[i];
	}

	// Set outputs
	float mix = 0.0;

	for (int i = 0; i < 4; i++) {
		if (outputs[OUT1_OUTPUT + i].active) {
			outputs[OUT1_OUTPUT + i].value = ins[i];
		}
		else {
			mix += ins[i];
		}
	}

	outputs[MIX_OUTPUT].value = clampf(mix / 2.0, -10.0, 10.0);

	// Set lights
	for (int i = 0; i < 4; i++) {
		lights[GAIN1_LIGHT + i].setBrightness(gains[i]);
	}

	if (poly_lfo_mode) {
		lights[EDIT_LIGHT].value = (poly_lfo.level(0) > 128 ? 1.0 : 0.0);
	}
	else {
		lights[EDIT_LIGHT].value = (nearestIndex >= 0 ? 1.0 : 0.0);
	}

	// Set frame light colors
	const uint8_t *colors;
	if (poly_lfo_mode) {
		colors = poly_lfo.color();
	}
	else {
		colors = keyframer.color();
	}
	for (int i = 0; i < 3; i++) {
		float c = colors[i] / 255.0;
		c = 1.0 - (1.0 - c) * 1.25;
		lights[FRAME_LIGHT + i].setBrightness(c);
	}
}


struct CKSSRot : SVGSwitch, ToggleSwitch {
	CKSSRot() {
		addFrame(SVG::load(assetPlugin(plugin, "res/CKSS_rot_0.svg")));
		addFrame(SVG::load(assetPlugin(plugin, "res/CKSS_rot_1.svg")));
		sw->wrap();
		box.size = sw->box.size;
	}
};


FramesWidget::FramesWidget() {
	Frames *module = new Frames();
	setModule(module);
	box.size = Vec(15*18, 380);

	{
		Panel *panel = new LightPanel();
		panel->backgroundImage = Image::load(assetPlugin(plugin, "res/Frames.png"));
		panel->box.size = box.size;
		addChild(panel);
	}

	addChild(createScrew<ScrewSilver>(Vec(15, 0)));
	addChild(createScrew<ScrewSilver>(Vec(box.size.x-30, 0)));
	addChild(createScrew<ScrewSilver>(Vec(15, 365)));
	addChild(createScrew<ScrewSilver>(Vec(box.size.x-30, 365)));

	addParam(createParam<Rogan1PSWhite>(Vec(14, 52), module, Frames::GAIN1_PARAM, 0.0, 1.0, 0.0));
	addParam(createParam<Rogan1PSWhite>(Vec(81, 52), module, Frames::GAIN2_PARAM, 0.0, 1.0, 0.0));
	addParam(createParam<Rogan1PSWhite>(Vec(149, 52), module, Frames::GAIN3_PARAM, 0.0, 1.0, 0.0));
	addParam(createParam<Rogan1PSWhite>(Vec(216, 52), module, Frames::GAIN4_PARAM, 0.0, 1.0, 0.0));
	addParam(createParam<Rogan6PSWhite>(Vec(89, 115), module, Frames::FRAME_PARAM, 0.0, 1.0, 0.0));
	addParam(createParam<Rogan1PSGreen>(Vec(208, 141), module, Frames::MODULATION_PARAM, -1.0, 1.0, 0.0));
	addParam(createParam<CKD6>(Vec(19, 123), module, Frames::ADD_PARAM, 0.0, 1.0, 0.0));
	addParam(createParam<CKD6>(Vec(19, 172), module, Frames::DEL_PARAM, 0.0, 1.0, 0.0));
	addParam(createParam<CKSSRot>(Vec(18, 239), module, Frames::OFFSET_PARAM, 0.0, 1.0, 0.0));

	addInput(createInput<PJ301MPort>(Vec(16, 273), module, Frames::ALL_INPUT));
	addInput(createInput<PJ301MPort>(Vec(59, 273), module, Frames::IN1_INPUT));
	addInput(createInput<PJ301MPort>(Vec(102, 273), module, Frames::IN2_INPUT));
	addInput(createInput<PJ301MPort>(Vec(145, 273), module, Frames::IN3_INPUT));
	addInput(createInput<PJ301MPort>(Vec(188, 273), module, Frames::IN4_INPUT));
	addInput(createInput<PJ301MPort>(Vec(231, 273), module, Frames::FRAME_INPUT));

	addOutput(createOutput<PJ301MPort>(Vec(16, 315), module, Frames::MIX_OUTPUT));
	addOutput(createOutput<PJ301MPort>(Vec(59, 315), module, Frames::OUT1_OUTPUT));
	addOutput(createOutput<PJ301MPort>(Vec(102, 315), module, Frames::OUT2_OUTPUT));
	addOutput(createOutput<PJ301MPort>(Vec(145, 315), module, Frames::OUT3_OUTPUT));
	addOutput(createOutput<PJ301MPort>(Vec(188, 315), module, Frames::OUT4_OUTPUT));
	addOutput(createOutput<PJ301MPort>(Vec(231, 315), module, Frames::FRAME_STEP_OUTPUT));

	addChild(createLight<SmallLight<GreenLight>>(Vec(30, 101), module, Frames::GAIN1_LIGHT + 0));
	addChild(createLight<SmallLight<GreenLight>>(Vec(97, 101), module, Frames::GAIN1_LIGHT + 1));
	addChild(createLight<SmallLight<GreenLight>>(Vec(165, 101), module, Frames::GAIN1_LIGHT + 2));
	addChild(createLight<SmallLight<GreenLight>>(Vec(232, 101), module, Frames::GAIN1_LIGHT + 3));
	addChild(createLight<MediumLight<GreenLight>>(Vec(61, 155), module, Frames::EDIT_LIGHT));

	struct FrameLight : RedGreenBlueLight {
		FrameLight() {
			box.size = Vec(71, 71);
		}
	};
	addChild(createLight<FrameLight>(Vec(100, 126), module, Frames::FRAME_LIGHT));
}


struct FramesCurveItem : MenuItem {
	Frames *frames;
	uint8_t channel;
	frames::EasingCurve curve;
	void onAction(EventAction &e) override {
		frames->keyframer.mutable_settings(channel)->easing_curve = curve;
	}
	void step() override {
		rightText = (frames->keyframer.mutable_settings(channel)->easing_curve == curve) ? "✔" : "";
		MenuItem::step();
	}
};


struct FramesResponseItem : MenuItem {
	Frames *frames;
	uint8_t channel;
	uint8_t response;
	void onAction(EventAction &e) override {
		frames->keyframer.mutable_settings(channel)->response = response;
	}
	void step() override {
		rightText = (frames->keyframer.mutable_settings(channel)->response == response) ? "✔" : "";
		MenuItem::step();
	}
};


struct FramesChannelSettingsItem : MenuItem {
	Frames *frames;
	uint8_t channel;
	Menu *createChildMenu() override {
		Menu *menu = new Menu();

		// TODO
		menu->addChild(construct<MenuLabel>(&MenuEntry::text, "Interpolation Curve"));
		menu->addChild(construct<FramesCurveItem>(&MenuEntry::text, "Step", &FramesCurveItem::frames, frames, &FramesCurveItem::channel, channel, &FramesCurveItem::curve, frames::EASING_CURVE_STEP));
		menu->addChild(construct<FramesCurveItem>(&MenuEntry::text, "Linear", &FramesCurveItem::frames, frames, &FramesCurveItem::channel, channel, &FramesCurveItem::curve, frames::EASING_CURVE_LINEAR));
		menu->addChild(construct<FramesCurveItem>(&MenuEntry::text, "Accelerating", &FramesCurveItem::frames, frames, &FramesCurveItem::channel, channel, &FramesCurveItem::curve, frames::EASING_CURVE_IN_QUARTIC));
		menu->addChild(construct<FramesCurveItem>(&MenuEntry::text, "Decelerating", &FramesCurveItem::frames, frames, &FramesCurveItem::channel, channel, &FramesCurveItem::curve, frames::EASING_CURVE_OUT_QUARTIC));
		menu->addChild(construct<FramesCurveItem>(&MenuEntry::text, "Smooth Departure/Arrival", &FramesCurveItem::frames, frames, &FramesCurveItem::channel, channel, &FramesCurveItem::curve, frames::EASING_CURVE_SINE));
		menu->addChild(construct<FramesCurveItem>(&MenuEntry::text, "Bouncing", &FramesCurveItem::frames, frames, &FramesCurveItem::channel, channel, &FramesCurveItem::curve, frames::EASING_CURVE_BOUNCE));
		menu->addChild(construct<MenuLabel>());
		menu->addChild(construct<MenuLabel>(&MenuEntry::text, "Response Curve"));
		menu->addChild(construct<FramesResponseItem>(&MenuEntry::text, "Linear", &FramesResponseItem::frames, frames, &FramesResponseItem::channel, channel, &FramesResponseItem::response, 0));
		menu->addChild(construct<FramesResponseItem>(&MenuEntry::text, "Exponential", &FramesResponseItem::frames, frames, &FramesResponseItem::channel, channel, &FramesResponseItem::response, 255));

		return menu;
	}
};


struct FramesClearItem : MenuItem {
	Frames *frames;
	void onAction(EventAction &e) override {
		frames->keyframer.Clear();
	}
};


struct FramesModeItem : MenuItem {
	Frames *frames;
	bool poly_lfo_mode;
	void onAction(EventAction &e) override {
		frames->poly_lfo_mode = poly_lfo_mode;
	}
	void step() override {
		rightText = (frames->poly_lfo_mode == poly_lfo_mode) ? "✔" : "";
		MenuItem::step();
	}
};


Menu *FramesWidget::createContextMenu() {
	Menu *menu = ModuleWidget::createContextMenu();

	Frames *frames = dynamic_cast<Frames*>(module);
	assert(frames);

	menu->addChild(construct<MenuLabel>());
	menu->addChild(construct<MenuLabel>(&MenuEntry::text, "Channel Settings"));
	for (int i = 0; i < 4; i++) {
		menu->addChild(construct<FramesChannelSettingsItem>(&MenuItem::text, stringf("Channel %d", i + 1), &FramesChannelSettingsItem::frames, frames, &FramesChannelSettingsItem::channel, i));
	}
	menu->addChild(construct<FramesClearItem>(&MenuItem::text, "Clear Keyframes", &FramesClearItem::frames, frames));

	menu->addChild(construct<MenuLabel>());
	menu->addChild(construct<MenuLabel>(&MenuEntry::text, "Mode"));
	menu->addChild(construct<FramesModeItem>(&MenuItem::text, "Keyframer", &FramesModeItem::frames, frames, &FramesModeItem::poly_lfo_mode, false));
	menu->addChild(construct<FramesModeItem>(&MenuItem::text, "Poly LFO", &FramesModeItem::frames, frames, &FramesModeItem::poly_lfo_mode, true));

	return menu;
}
