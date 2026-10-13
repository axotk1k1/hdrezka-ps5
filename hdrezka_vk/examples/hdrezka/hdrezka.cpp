/*
 * HdRezka - its program, made from the PS5 Vulkan Template's UI starter.
 *
 * One complete design of PS5_VKHomebrewUI's kit (ps5/ui/kit.hpp) fills the
 * screen: its procedural backdrop, its scene, its frosted overlay and its post
 * overlay, its sounds on the console's audio output, its rumble and its light
 * bar colour. There is no switcher: every button is the design's, and holding
 * OPTIONS for a second leaves (back to the menu, or out of a title of one
 * program). The design is this title's copy of the kit's "store", in
 * kit/screen.cpp, to change at will.
 *
 * Copyright (C) 2026 Mihawk
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "logger.hpp"

// The design on screen: this title's own, in kit/screen.cpp
std::unique_ptr<hui::app::Concept> makeScreen(hui::app::Context &context)
{
	write_log("[DEBUG] makeScreen started\n");
	auto s = ps5ui::make_screen(context);
	write_log("[DEBUG] makeScreen finished\n");
	return s;
}

class VulkanExample : public ps5ui::KitExample
{
public:
	// What a design receives: the fonts, the sample content, the live numbers
	// it may show and the settings it may change
	hui::Settings choices;
	hui::app::Telemetry telemetry;
	std::unique_ptr<hui::app::Context> context;
	std::unique_ptr<hui::app::Concept> screen;
	hui::app::Frame frame;
	hui::ui::Feedback feedback;
	ps5ui::HoldToLeave leaving;
	hui::gfx::DrawList leavingList;
	double fpsSeconds{ 0.0 };
	int fpsFrames{ 0 };

	VulkanExample() : KitExample()
	{
		title = "HdRezka";
		name = "hdrezka";
		// The kit draws the whole screen and owns the pad
		settings.overlay = false;
		ps5.ownOverlay = true;
		defaultClearColor = { { 0.0f, 0.0f, 0.0f, 1.0f } };
	}

	void prepare() override
	{
		write_log("[DEBUG] ------------------\n");
		write_log("[DEBUG] VulkanExample::prepare started\n");
		VulkanExampleBase::prepare();
		write_log("[DEBUG] VulkanExampleBase::prepare done\n");
		prepareKit({ .music = false });
		write_log("[DEBUG] prepareKit done (music off)\n");
		rezka::App::instance().init(vulkanDevice, queue, &kit.renderer, &kit.mixer);
		write_log("[DEBUG] App::instance().init done\n");
		context = std::make_unique<hui::app::Context>(hui::app::Context{ kit.fonts, kit.catalog, telemetry, choices });
		write_log("[DEBUG] Context created\n");
		screen = makeScreen(*context);
		write_log("[DEBUG] makeScreen done\n");
		screen->enter();
		write_log("[DEBUG] screen->enter done\n");
		kit.apply(choices);
		feedback.play(hui::audio::Cue::welcome);
		prepared = true;
		write_log("[DEBUG] VulkanExample::prepare finished\n");
	}

	// Holding OPTIONS for a second leaves; a press is the design's
	void holdOptions(float dt)
	{
		leaving.label = ps5.optionsEnds ? "Back to the menu" : "Close";
		if (!ps5.frameBudget && leaving.update(dt, (ps5_pad().held & PAD_OPTIONS) != 0)) {
			quit = true;
		}
	}

	void updateTelemetry()
	{
		if (benchmark.active) {
			// A test run's pictures must not depend on the console's timing
			telemetry.push(16.7f);
			telemetry.fps = 59.9f;
			telemetry.average_ms = 16.7f;
		} else {
			telemetry.push(frameTimer * 1000.0f);
			fpsSeconds += frameTimer;
			fpsFrames++;
			if (fpsSeconds >= 0.5) {
				telemetry.fps = (float)(fpsFrames / fpsSeconds);
				telemetry.average_ms = (float)(fpsSeconds * 1000.0 / fpsFrames);
				fpsSeconds = 0.0;
				fpsFrames = 0;
			}
			telemetry.voices = kit.mixer.active_voices();
		}
		telemetry.draw_calls = kit.renderer.last_draw_calls();
		telemetry.instances = kit.renderer.last_instances();
	}

	void render() override
	{
		if (!prepared) {
			return;
		}
        static int render_logs = 0;
        if (render_logs < 10) { write_log("[DEBUG] render() frame %d start\n", render_logs); }
		prepareFrame();
		const float dt = std::min(frameTimer, 0.05f);
		// A test run ignores the pad: the design shows its entrance and rests
		hui::InputFrame input = kit.input();
		if (ps5.frameBudget) {
			input = hui::InputFrame{};
			input.connected = true;
		}
		holdOptions(dt);
		updateTelemetry();
        if (render_logs < 10) { write_log("[DEBUG] render() App::instance().update\n"); }
		rezka::App::instance().update(dt);
        if (render_logs < 10) { write_log("[DEBUG] render() screen->update\n"); }
		screen->update(input, dt, feedback);
		// CIRCLE the design did not take (it answers the CIRCLE it uses with a cue) leaves
		if (!ps5.frameBudget && input.is_pressed(hui::Action::back) && feedback.cues.empty()) {
			quit = true;
		}
		if (context->settings_changed) {
			context->settings_changed = false;
			kit.apply(choices);
		}
		kit.play(feedback, screen->info().sounds, choices.haptics);
		feedback.clear();
		if (choices.light_bar) {
			kit.light_bar(screen->info().accent);
		}
		kit.tick(dt);

		// backdrop, scene, [the glass copy], overlay, post: the order a design is drawn in
		frame.reset();
		frame.glass_texture = kit.renderer.glass_texture();
        if (render_logs < 10) { write_log("[DEBUG] render() screen->draw\n"); }
		screen->draw(frame);
        if (render_logs < 10) { write_log("[DEBUG] render() kit.renderer\n"); }
		kit.renderer.begin();
		kit.renderer.backdrop(frame.backdrop);
		kit.renderer.draw(frame.scene);
		if (frame.glass) {
			kit.renderer.glass();
		}
		kit.renderer.draw(frame.overlay);
		kit.renderer.backdrop(frame.post);
		leavingList.clear();
		leaving.draw(leavingList, kit.fonts, ps5ui::active_theme());
		kit.renderer.draw(leavingList);
        if (render_logs < 10) { write_log("[DEBUG] render() buildKitCommandBuffer\n"); }
		buildKitCommandBuffer();
        if (render_logs < 10) { write_log("[DEBUG] render() submitFrame\n"); }
		submitFrame();
        if (render_logs < 10) { write_log("[DEBUG] render() frame %d end\n", render_logs); render_logs++; }
	}

	~VulkanExample() override
	{
		// The catalogue's worker may still be fetching posters: it ends here,
		// not during the process's exit handlers, where the TLS library it
		// is in is already being torn down.
		rezka::App::instance().shutdown();
		if (device) {
			vkDeviceWaitIdle(device);
		}
	}
};

VULKAN_EXAMPLE_MAIN()
