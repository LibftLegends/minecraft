#ifndef APPLICATION_PHASE_CONTROLLER_HPP
# define APPLICATION_PHASE_CONTROLLER_HPP

# include "../../src/app/GameSession.hpp"
# include "../../src/menu/MenuCanvas.hpp"
# include "../../src/menu/MenuController.hpp"
# include "../../src/platform/ApplicationWindow.hpp"
# include "../../src/policy/RenderDistanceStrategy.hpp"
# include "../../src/render/VoxelRenderer.hpp"
# include "../ft_vox.hpp"

class ApplicationPhaseController
{
  public:
	enum class Phase
	{
		MAIN_MENU,
		LOADING,
		IN_GAME,
		IN_GAME_SETTINGS
	};

	ApplicationPhaseController();
	ApplicationPhaseController(const ApplicationPhaseController &other);
	~ApplicationPhaseController();
	ApplicationPhaseController &operator=(const ApplicationPhaseController &other);

	static Phase tick_phase(Phase phase, ApplicationWindow &window,
		MenuController &menu, GameSession &session, VoxelRenderer &renderer,
		double dt, int &loading_frames, const RenderDistanceStrategy &strategy);
	static void render_frame(Phase phase, ApplicationWindow &window,
		MenuController &menu, GameSession &session, VoxelRenderer &renderer);

  private:
	static MenuCanvas::MenuInput collect_menu_input(ApplicationWindow &window);
	static Phase tick_menu(Phase phase, ApplicationWindow &window,
		MenuController &menu, GameSession &session, VoxelRenderer &renderer,
		int &loading_frames, const RenderDistanceStrategy &strategy);
	static Phase tick_in_game(ApplicationWindow &window, MenuController &menu,
		GameSession &session, VoxelRenderer &renderer, double dt,
		const RenderDistanceStrategy &strategy);
	static Phase tick_in_game_settings(ApplicationWindow &window,
		MenuController &menu, GameSession &session);
	static void render_gpu_frame(Phase phase, ApplicationWindow &window,
		MenuController &menu, GameSession &session, VoxelRenderer &renderer);
	static void render_cpu_frame(Phase phase, ApplicationWindow &window,
		MenuController &menu, GameSession &session, VoxelRenderer &renderer);
};

#endif
