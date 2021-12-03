// SPDX-License-Identifier: GPL-2.0
#include "profilescene.h"
#include "diveeventitem.h"
#include "divecartesianaxis.h"
#include "divepercentageitem.h"
#include "divepixmapcache.h"
#include "diveprofileitem.h"
#include "divetextitem.h"
#include "tankitem.h"
#include "core/device.h"
#include "core/event.h"
#include "core/pref.h"
#include "core/profile.h"
#include "core/qthelper.h"	// for decoMode()
#include "core/subsurface-string.h"
#include "core/settings/qPrefDisplay.h"
#include "qt-models/diveplotdatamodel.h"
#include "qt-models/diveplannermodel.h"
#include <QAbstractAnimation>

static const double diveComputerTextBorder = 1.0;

// Class for animations (if any). Might want to do our own.
class ProfileAnimation : public QAbstractAnimation {
	ProfileScene &scene;
	// For historical reasons, speed is actually the duration
	// (i.e. the reciprocal of speed). Ouch, that hurts.
	int speed;

	int duration() const override
	{
		return speed;
	}
	void updateCurrentTime(int time) override
	{
		// Note: we explicitly pass 1.0 at the end, so that
		// the callee can do a simple float comparison for "end".
		scene.anim(time == speed ? 1.0
					 : static_cast<double>(time) / speed);
	}
public:
	ProfileAnimation(ProfileScene &scene, int animSpeed) :
		scene(scene),
		speed(animSpeed)
	{
		start();
	}
};

template<typename T, class... Args>
T *ProfileScene::createItem(const DiveCartesianAxis &vAxis, int vColumn, int z, Args&&... args)
{
	T *res = new T(*dataModel, *timeAxis, vAxis, vColumn, std::forward<Args>(args)...);
	res->setZValue(static_cast<double>(z));
	profileItems.push_back(res);
	return res;
}

PartialPressureGasItem *ProfileScene::createPPGas(int column, color_index_t color, color_index_t colorAlert,
						    const double *thresholdSettingsMin, const double *thresholdSettingsMax)
{
	PartialPressureGasItem *item = createItem<PartialPressureGasItem>(*gasYAxis, column, 99, dpr);
	item->setThresholdSettingsKey(thresholdSettingsMin, thresholdSettingsMax);
	item->setColors(getColor(color, isGrayscale), getColor(colorAlert, isGrayscale));
	return item;
}

ProfileScene::ProfileScene(double dpr, bool printMode, bool isGrayscale) :
	d(nullptr),
	dc(-1),
	dpr(dpr),
	printMode(printMode),
	isGrayscale(isGrayscale),
	maxtime(-1),
	maxdepth(-1),
	dataModel(new DivePlotDataModel(this)),
	profileYAxis(new DiveCartesianAxis(DiveCartesianAxis::Position::Left, true, 3, 0, TIME_GRID, Qt::red, true, true,
				   dpr, 1.0, printMode, isGrayscale, *this)),
	gasYAxis(new DiveCartesianAxis(DiveCartesianAxis::Position::Right, false, 1, 2, TIME_GRID, Qt::black, true, true,
				       dpr, 0.7, printMode, isGrayscale, *this)),
	temperatureAxis(new DiveCartesianAxis(DiveCartesianAxis::Position::Right, false, 3, 0, TIME_GRID, Qt::black, false, false,
					    dpr, 1.0, printMode, isGrayscale, *this)),
	timeAxis(new DiveCartesianAxis(DiveCartesianAxis::Position::Bottom, false, 2, 2, TIME_GRID, Qt::blue, true, true,
			      dpr, 1.0, printMode, isGrayscale, *this)),
	cylinderPressureAxis(new DiveCartesianAxis(DiveCartesianAxis::Position::Right, false, 4, 0, TIME_GRID, Qt::black, false, false,
						   dpr, 1.0, printMode, isGrayscale, *this)),
	heartBeatAxis(new DiveCartesianAxis(DiveCartesianAxis::Position::Left, false, 3, 0, HR_AXIS, Qt::black, true, true,
					    dpr, 0.7, printMode, isGrayscale, *this)),
	percentageAxis(new DiveCartesianAxis(DiveCartesianAxis::Position::Right, false, 2, 0, TIME_GRID, Qt::black, false, false,
					     dpr, 0.7, printMode, isGrayscale, *this)),
	diveProfileItem(createItem<DiveProfileItem>(*profileYAxis, DivePlotDataModel::DEPTH, 0, dpr)),
	temperatureItem(createItem<DiveTemperatureItem>(*temperatureAxis, DivePlotDataModel::TEMPERATURE, 1, dpr)),
	meanDepthItem(createItem<DiveMeanDepthItem>(*profileYAxis, DivePlotDataModel::INSTANT_MEANDEPTH, 1, dpr)),
	gasPressureItem(createItem<DiveGasPressureItem>(*cylinderPressureAxis, DivePlotDataModel::TEMPERATURE, 1, dpr)),
	diveComputerText(new DiveTextItem(dpr, 1.0, Qt::AlignRight | Qt::AlignTop, nullptr)),
	reportedCeiling(createItem<DiveReportedCeiling>(*profileYAxis, DivePlotDataModel::CEILING, 1, dpr)),
	pn2GasItem(createPPGas(DivePlotDataModel::PN2, PN2, PN2_ALERT, NULL, &prefs.pp_graphs.pn2_threshold)),
	pheGasItem(createPPGas(DivePlotDataModel::PHE, PHE, PHE_ALERT, NULL, &prefs.pp_graphs.phe_threshold)),
	po2GasItem(createPPGas(DivePlotDataModel::PO2, PO2, PO2_ALERT, &prefs.pp_graphs.po2_threshold_min, &prefs.pp_graphs.po2_threshold_max)),
	o2SetpointGasItem(createPPGas(DivePlotDataModel::O2SETPOINT, O2SETPOINT, PO2_ALERT, &prefs.pp_graphs.po2_threshold_min, &prefs.pp_graphs.po2_threshold_max)),
	ccrsensor1GasItem(createPPGas(DivePlotDataModel::CCRSENSOR1, CCRSENSOR1, PO2_ALERT, &prefs.pp_graphs.po2_threshold_min, &prefs.pp_graphs.po2_threshold_max)),
	ccrsensor2GasItem(createPPGas(DivePlotDataModel::CCRSENSOR2, CCRSENSOR2, PO2_ALERT, &prefs.pp_graphs.po2_threshold_min, &prefs.pp_graphs.po2_threshold_max)),
	ccrsensor3GasItem(createPPGas(DivePlotDataModel::CCRSENSOR3, CCRSENSOR3, PO2_ALERT, &prefs.pp_graphs.po2_threshold_min, &prefs.pp_graphs.po2_threshold_max)),
	ocpo2GasItem(createPPGas(DivePlotDataModel::SCR_OC_PO2, SCR_OCPO2, PO2_ALERT, &prefs.pp_graphs.po2_threshold_min, &prefs.pp_graphs.po2_threshold_max)),
	diveCeiling(createItem<DiveCalculatedCeiling>(*profileYAxis, DivePlotDataModel::CEILING, 1, dpr)),
	decoModelParameters(new DiveTextItem(dpr, 1.0, Qt::AlignHCenter | Qt::AlignTop, nullptr)),
	heartBeatItem(createItem<DiveHeartrateItem>(*heartBeatAxis, DivePlotDataModel::HEARTBEAT, 1, dpr)),
	percentageItem(new DivePercentageItem(*timeAxis, *percentageAxis, dpr)),
	tankItem(new TankItem(*timeAxis, dpr)),
	pixmaps(getDivePixmaps(dpr))
{
	init_plot_info(&plotInfo);

	setSceneRect(0, 0, 100, 100);
	setItemIndexMethod(QGraphicsScene::NoIndex);

	gasYAxis->setZValue(timeAxis->zValue() + 1);
	tankItem->setZValue(100);

	// These axes are not locale-dependent. Set their scale factor once here.
	timeAxis->setTransform(1.0/60.0);
	heartBeatAxis->setTransform(1.0);
	gasYAxis->setTransform(1.0); // Non-metric countries likewise use bar (disguised as "percentage") for partial pressure.

	for (int i = 0; i < 16; i++) {
		DiveCalculatedTissue *tissueItem = createItem<DiveCalculatedTissue>(*profileYAxis, DivePlotDataModel::TISSUE_1 + i, i + 1, dpr);
		allTissues.append(tissueItem);
	}

	percentageItem->setZValue(1.0);

	// Add items to scene
	addItem(diveComputerText);
	addItem(tankItem);
	addItem(decoModelParameters);
	addItem(profileYAxis);
	addItem(gasYAxis);
	addItem(temperatureAxis);
	addItem(timeAxis);
	addItem(cylinderPressureAxis);
	addItem(percentageAxis);
	addItem(heartBeatAxis);
	addItem(percentageItem);

	for (AbstractProfilePolygonItem *item: profileItems)
		addItem(item);
}

ProfileScene::~ProfileScene()
{
	free_plot_info_data(&plotInfo);
}

void ProfileScene::clear()
{
	dataModel->clear();

	for (AbstractProfilePolygonItem *item: profileItems)
		item->clear();

	// the events will have connected slots which can fire after
	// the dive and its data have been deleted - so explictly delete
	// the DiveEventItems
	qDeleteAll(eventItems);
	eventItems.clear();
}

static bool ppGraphsEnabled(const struct divecomputer *dc, bool simplified)
{
	return simplified ? (dc->divemode == CCR && prefs.pp_graphs.po2)
			  : (prefs.pp_graphs.po2 || prefs.pp_graphs.pn2 || prefs.pp_graphs.phe);
}

// Update visibility of non-interactive chart features according to preferences
void ProfileScene::updateVisibility(bool diveHasHeartBeat, bool simplified)
{
	const struct divecomputer *currentdc = get_dive_dc_const(d, dc);
	if (!currentdc)
		return;
	bool ppGraphs = ppGraphsEnabled(currentdc, simplified);

	if (simplified) {
		pn2GasItem->setVisible(false);
		po2GasItem->setVisible(ppGraphs);
		pheGasItem->setVisible(false);

		temperatureItem->setVisible(!ppGraphs);
		tankItem->setVisible(!ppGraphs && prefs.tankbar);

		o2SetpointGasItem->setVisible(ppGraphs && prefs.show_ccr_setpoint);
		ccrsensor1GasItem->setVisible(ppGraphs && prefs.show_ccr_sensors);
		ccrsensor2GasItem->setVisible(ppGraphs && prefs.show_ccr_sensors && (currentdc->no_o2sensors > 1));
		ccrsensor3GasItem->setVisible(ppGraphs && prefs.show_ccr_sensors && (currentdc->no_o2sensors > 1));
		ocpo2GasItem->setVisible((currentdc->divemode == PSCR) && prefs.show_scr_ocpo2);
	} else {
		pn2GasItem->setVisible(prefs.pp_graphs.pn2);
		po2GasItem->setVisible(prefs.pp_graphs.po2);
		pheGasItem->setVisible(prefs.pp_graphs.phe);

		bool setpointflag = currentdc->divemode == CCR && prefs.pp_graphs.po2;
		bool sensorflag = setpointflag && prefs.show_ccr_sensors;
		o2SetpointGasItem->setVisible(setpointflag && prefs.show_ccr_setpoint);
		ccrsensor1GasItem->setVisible(sensorflag);
		ccrsensor2GasItem->setVisible(sensorflag && currentdc->no_o2sensors > 1);
		ccrsensor3GasItem->setVisible(sensorflag && currentdc->no_o2sensors > 2);
		ocpo2GasItem->setVisible(currentdc->divemode == PSCR && prefs.show_scr_ocpo2);

		heartBeatItem->setVisible(prefs.hrgraph && diveHasHeartBeat);

		diveCeiling->setVisible(prefs.calcceiling);
		decoModelParameters->setVisible(prefs.decoinfo);

		for (DiveCalculatedTissue *tissue: allTissues)
			tissue->setVisible(prefs.calcalltissues && prefs.calcceiling);
		percentageItem->setVisible(prefs.percentagegraph);

		meanDepthItem->setVisible(prefs.show_average_depth);
		reportedCeiling->setVisible(prefs.dcceiling);
		tankItem->setVisible(prefs.tankbar);
		temperatureItem->setVisible(true);
	}
}

void ProfileScene::resize(QSizeF size)
{
	setSceneRect(QRectF(QPointF(), size));
}

// Helper templates to determine slope and intersect of a linear function.
// The function arguments are supposed to be integral types.
template<typename Func>
static auto intercept(Func f)
{
	return f(0);
}
template<typename Func>
static auto slope(Func f)
{
	return f(1) - f(0);
}

// Helper structure for laying out secondary plots.
struct VerticalAxisLayout {
	DiveCartesianAxis *axis;
	double height;
	bool visible;
};

void ProfileScene::updateAxes(bool diveHasHeartBeat, bool simplified)
{
	const struct divecomputer *currentdc = get_dive_dc_const(d, dc);
	if (!currentdc)
		return;

	// Calculate left and right border needed for the axes and other chart items.
	double leftBorder = profileYAxis->width();
	if (prefs.hrgraph)
		leftBorder = std::max(leftBorder, heartBeatAxis->width());

	double rightWidth = timeAxis->horizontalOverhang();
	if (prefs.show_average_depth)
		rightWidth = std::max(rightWidth, meanDepthItem->labelWidth);
	if (ppGraphsEnabled(currentdc, simplified))
		rightWidth = std::max(rightWidth, gasYAxis->width());
	double rightBorder = sceneRect().width() - rightWidth;
	double width = rightBorder - leftBorder;

	if (width <= 10.0 * dpr)
		return clear();

	// Place the fixed dive computer text at the bottom
	double bottomBorder = sceneRect().height() - diveComputerText->height() - 2.0 * dpr * diveComputerTextBorder;
	diveComputerText->setPos(0.0, bottomBorder + dpr * diveComputerTextBorder);

	double topBorder = 0.0;

	// show the deco model parameters at the top in the center
	if (prefs.decoinfo) {
		decoModelParameters->setPos(leftBorder + width / 2.0, topBorder);
		topBorder += decoModelParameters->height();
	}

	bottomBorder -= timeAxis->height();
	timeAxis->setPosition(QRectF(leftBorder, topBorder, width, bottomBorder - topBorder));

	if (prefs.tankbar) {
		bottomBorder -= tankItem->height();
		// Note: we set x to 0.0, because the tank item uses the timeAxis to set the x-coordinate.
		tankItem->setPos(0.0, bottomBorder);
	}

	double height = bottomBorder - topBorder;
	if (height <= 50.0 * dpr)
		return clear();

	// The rest is laid out dynamically. Give at least 50% to the actual profile.
	// The max heights are given for DPR=1, i.e. a ca. 800x600 pixels profile.
	const double minProfileFraction = 0.5;
        VerticalAxisLayout secondaryAxes[] = {
		// Note: axes are listed from bottom to top, since they are added that way.
		{ heartBeatAxis, 75.0, prefs.hrgraph && diveHasHeartBeat },
		{ percentageAxis, 50.0, prefs.percentagegraph },
		{ gasYAxis, 75.0, ppGraphsEnabled(currentdc, simplified) },
		{ temperatureAxis, 50.0, true },
        };

	// A loop is probably easier to read than std::accumulate.
	double totalSecondaryHeight = 0.0;
	for (const VerticalAxisLayout &l: secondaryAxes) {
		if (l.visible)
			totalSecondaryHeight += l.height * dpr;
	}

	if (totalSecondaryHeight > height * minProfileFraction) {
		// Use 50% for the profile and the rest for the remaining graphs, scaled by their maximum height.
		double remainingSpace = height * minProfileFraction;
		for (VerticalAxisLayout &l: secondaryAxes)
			l.height *= remainingSpace / totalSecondaryHeight;
	}

	for (const VerticalAxisLayout &l: secondaryAxes) {
		l.axis->setVisible(l.visible);
		if (!l.visible)
			continue;
		bottomBorder -= l.height * dpr;
		l.axis->setPosition(QRectF(leftBorder, bottomBorder, width, l.height * dpr));
	}

	height = bottomBorder - topBorder;
	profileYAxis->setPosition(QRectF(leftBorder, topBorder, width, height));

	// The cylinders are displayed in the 24-80% region of the profile
	cylinderPressureAxis->setPosition(QRectF(leftBorder, topBorder + 0.24 * height, width, 0.56 * height));

	// Set scale factors depending on locale.
	// The conversion calls, such as mm_to_feet(), will be optimized away.
	profileYAxis->setTransform(prefs.units.length == units::METERS ? 0.001 : slope(mm_to_feet));
	cylinderPressureAxis->setTransform(prefs.units.pressure == units::BAR ? 0.001 : slope(mbar_to_PSI));
	// Temperature is special: this is not a linear transformation, but requires a shift of origin.
	if (prefs.units.temperature == units::CELSIUS)
		temperatureAxis->setTransform(slope(mkelvin_to_C), intercept(mkelvin_to_C));
	else
		temperatureAxis->setTransform(slope(mkelvin_to_F), intercept(mkelvin_to_F));
}

bool ProfileScene::pointOnProfile(const QPointF &point) const
{
	return timeAxis->pointInRange(point.x()) && profileYAxis->pointInRange(point.y());
}

static double max_gas(const plot_info &pi, double gas_pressures::*gas)
{
	double ret = -1;
	for (int i = 0; i < pi.nr; ++i) {
		if (pi.entry[i].pressures.*gas > ret)
			ret = pi.entry[i].pressures.*gas;
	}
	return ret;
}

void ProfileScene::plotDive(const struct dive *dIn, int dcIn, DivePlannerPointsModel *plannerModel,
			   bool inPlanner, bool instant, bool keepPlotInfo, bool calcMax, double zoom, double zoomedPosition)
{
	d = dIn;
	dc = dcIn;
	animatedAxes.clear();
	if (!d) {
		clear();
		return;
	}

	if (!plannerModel) {
		if (decoMode(false) == VPMB)
			decoModelParameters->set(QString("VPM-B +%1").arg(prefs.vpmb_conservatism), getColor(PRESSURE_TEXT));
		else
			decoModelParameters->set(QString("GF %1/%2").arg(prefs.gflow).arg(prefs.gfhigh), getColor(PRESSURE_TEXT));
	} else {
		struct diveplan &diveplan = plannerModel->getDiveplan();
		if (decoMode(inPlanner) == VPMB)
			decoModelParameters->set(QString("VPM-B +%1").arg(diveplan.vpmb_conservatism), getColor(PRESSURE_TEXT));
		else
			decoModelParameters->set(QString("GF %1/%2").arg(diveplan.gflow).arg(diveplan.gfhigh), getColor(PRESSURE_TEXT));
	}

	const struct divecomputer *currentdc = get_dive_dc_const(d, dc);
	if (!currentdc || !currentdc->samples)
		return;

	int animSpeed = instant || printMode ? 0 : qPrefDisplay::animation_speed();

	// A non-null planner_ds signals to create_plot_info_new that the dive is currently planned.
	struct deco_state *planner_ds = inPlanner && plannerModel ? &plannerModel->final_deco_state : nullptr;

	/* This struct holds all the data that's about to be plotted.
	 * I'm not sure this is the best approach ( but since we are
	 * interpolating some points of the Dive, maybe it is... )
	 * The  Calculation of the points should be done per graph,
	 * so I'll *not* calculate everything if something is not being
	 * shown.
	 * create_plot_info_new() automatically frees old plot data.
	 */
	if (!keepPlotInfo)
		create_plot_info_new(d, get_dive_dc_const(d, dc), &plotInfo, !calcMax, planner_ds);

	bool hasHeartBeat = plotInfo.maxhr;
	// For mobile we might want to turn of some features that are normally shown.
#ifdef SUBSURFACE_MOBILE
	bool simplified = true;
#else
	bool simplified = false;
#endif
	updateVisibility(hasHeartBeat, simplified);
	updateAxes(hasHeartBeat, simplified);

	int newMaxtime = get_maxtime(&plotInfo);
	if (calcMax || newMaxtime > maxtime)
		maxtime = newMaxtime;

	/* Only update the max. depth if it's bigger than the current ones
	 * when we are dragging the handler to plan / add dive.
	 * otherwhise, update normally.
	 */
	int newMaxDepth = get_maxdepth(&plotInfo);
	if (!calcMax) {
		if (maxdepth < newMaxDepth) {
			maxdepth = newMaxDepth;
		}
	} else {
		maxdepth = newMaxDepth;
	}

	dataModel->setDive(plotInfo);

	// It seems that I'll have a lot of boilerplate setting the model / axis for
	// each item, I'll mostly like to fix this in the future, but I'll keep at this for now.
	profileYAxis->setBounds(0.0, maxdepth);
	profileYAxis->updateTicks(animSpeed);
	animatedAxes.push_back(profileYAxis);

	temperatureAxis->setBounds(plotInfo.mintemp,
				   plotInfo.maxtemp - plotInfo.mintemp > 2000 ? plotInfo.maxtemp : plotInfo.mintemp + 2000);

	if (hasHeartBeat) {
		heartBeatAxis->setBounds(plotInfo.minhr, plotInfo.maxhr);
		heartBeatAxis->updateTicks(animSpeed);
		animatedAxes.push_back(heartBeatAxis);
	}

	percentageAxis->setBounds(0, 100);
	percentageAxis->setVisible(false);
	percentageAxis->updateTicks(animSpeed);
	animatedAxes.push_back(percentageAxis);

	if (calcMax) {
		double relStart = (1.0 - 1.0/zoom) * zoomedPosition;
		double relEnd = relStart + 1.0/zoom;
		timeAxis->setBounds(round(relStart * maxtime), round(relEnd * maxtime));
	}

	// Find first and last plotInfo entry
	int firstSecond = lrint(timeAxis->minimum());
	int lastSecond = lrint(timeAxis->maximum());
	auto it1 = std::lower_bound(plotInfo.entry, plotInfo.entry + plotInfo.nr, firstSecond,
				   [](const plot_data &d, int s)
				   { return d.sec < s; });
	auto it2 = std::lower_bound(it1, plotInfo.entry + plotInfo.nr, lastSecond,
				    [](const plot_data &d, int s)
				    { return d.sec < s; });
	if (it1 > plotInfo.entry && it1->sec > firstSecond)
		--it1;
	if (it2 < plotInfo.entry + plotInfo.nr)
		++it2;
	int from = it1 - plotInfo.entry;
	int to = it2 - plotInfo.entry;

	timeAxis->updateTicks(animSpeed);
	animatedAxes.push_back(timeAxis);
	cylinderPressureAxis->setBounds(plotInfo.minpressure, plotInfo.maxpressure);

	tankItem->setData(d, firstSecond, lastSecond);

	if (ppGraphsEnabled(currentdc, simplified)) {
		double max = prefs.pp_graphs.phe ? max_gas(dataModel->data(), &gas_pressures::he) : -1;
		if (prefs.pp_graphs.pn2)
			max = std::max(max_gas(dataModel->data(), &gas_pressures::n2), max);
		if (prefs.pp_graphs.po2)
			max = std::max(max_gas(dataModel->data(), &gas_pressures::o2), max);

		gasYAxis->setBounds(0.0, max);
		gasYAxis->updateTicks(animSpeed);
		animatedAxes.push_back(gasYAxis);
	}

	// Replot dive items
	for (AbstractProfilePolygonItem *item: profileItems)
		item->replot(d, from, to, inPlanner);

	if (prefs.percentagegraph)
		percentageItem->replot(d, currentdc, dataModel->data());

	// The event items are a bit special since we don't know how many events are going to
	// exist on a dive, so I cant create cache items for that. that's why they are here
	// while all other items are up there on the constructor.
	qDeleteAll(eventItems);
	eventItems.clear();
	struct event *event = currentdc->events;
	struct gasmix lastgasmix = get_gasmix_at_time(d, get_dive_dc_const(d, dc), duration_t{1});

	while (event) {
		// if print mode is selected only draw headings, SP change, gas events or bookmark event
		if (printMode) {
			if (empty_string(event->name) ||
			    !(strcmp(event->name, "heading") == 0 ||
			      (same_string(event->name, "SP change") && event->time.seconds == 0) ||
			      event_is_gaschange(event) ||
			      event->type == SAMPLE_EVENT_BOOKMARK)) {
				event = event->next;
				continue;
			}
		}
		if (DiveEventItem::isInteresting(d, currentdc, event, plotInfo, firstSecond, lastSecond)) {
			auto item = new DiveEventItem(d, event, lastgasmix, plotInfo,
						      timeAxis, profileYAxis, animSpeed, *pixmaps);
			item->setZValue(2);
			addItem(item);
			eventItems.push_back(item);
		}
		if (event_is_gaschange(event))
			lastgasmix = get_gasmix_from_event(d, event);
		event = event->next;
	}

	QString dcText = get_dc_nickname(currentdc);
	if (dcText == "planned dive")
		dcText = tr("Planned dive");
	else if (dcText == "manually added dive")
		dcText = tr("Manually added dive");
	else if (dcText.isEmpty())
		dcText = tr("Unknown dive computer");
#ifndef SUBSURFACE_MOBILE
	int nr;
	if ((nr = number_of_computers(d)) > 1)
		dcText += tr(" (#%1 of %2)").arg(dc + 1).arg(nr);
#endif
	diveComputerText->set(dcText, getColor(TIME_TEXT, isGrayscale));

	// Reset animation.
	if (animSpeed <= 0)
		animation.reset();
	else
		animation = std::make_unique<ProfileAnimation>(*this, animSpeed);
}

void ProfileScene::anim(double fraction)
{
	for (DiveCartesianAxis *axis: animatedAxes)
		axis->anim(fraction);
}

void ProfileScene::draw(QPainter *painter, const QRect &pos,
			const struct dive *d, int dc,
			DivePlannerPointsModel *plannerModel, bool inPlanner)
{
	QSize size = pos.size();
	resize(QSizeF(size));
	plotDive(d, dc, plannerModel, inPlanner, true, true);

	QImage image(pos.size(), QImage::Format_ARGB32);
	image.fill(getColor(::BACKGROUND, isGrayscale));

	QPainter imgPainter(&image);
	imgPainter.setRenderHint(QPainter::Antialiasing);
	imgPainter.setRenderHint(QPainter::SmoothPixmapTransform);
	render(&imgPainter, QRect(QPoint(), size), sceneRect(), Qt::IgnoreAspectRatio);
	imgPainter.end();

	if (isGrayscale) {
		// convert QImage to grayscale before rendering
		for (int i = 0; i < image.height(); i++) {
			QRgb *pixel = reinterpret_cast<QRgb *>(image.scanLine(i));
			QRgb *end = pixel + image.width();
			for (; pixel != end; pixel++) {
				int gray_val = qGray(*pixel);
				*pixel = QColor(gray_val, gray_val, gray_val).rgb();
			}
		}
	}
	painter->drawImage(pos, image);
}
