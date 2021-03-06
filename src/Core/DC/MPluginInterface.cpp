﻿#include "MPluginInterface.h"

#include <QTextCodec>
#include <QDate>
#include <QAction>
#include <QActionGroup>
#include <QDebug>
#include <QColor>

#include <osgText/Text>
#include <osg/LineWidth>
#include <osg/Point>
#include <osg/PositionAttitudeTransform>
#include <osgViewer/View>
#include <SettingsManager.h>
#include <osgEarth/TerrainLayer>
#include <osgEarth/Viewpoint>

static QColor  vecToColor(const osg::Vec4 &vec)
{
  return QColor(vec[0] * 255, vec[1] * 255, vec[2] * 255, vec[3] * 255);
}

static osg::Vec4  colorToVec(const QColor &color)
{
  return osg::Vec4(color.redF(), color.greenF(), color.blueF(), color.alphaF());
}

osg::Group   *MPluginInterface::_activatedPlugin = NULL;
QActionGroup *MPluginInterface::_mutexActions    = NULL;
QAction      *MPluginInterface::_defaultAction   = NULL;

// A time limit to tell apart double click and single click
static double  clickLimit;
static double  lastClicked = 0.0;

MPluginInterface::MPluginInterface():
  _instanceCount(0),
  _isDrawing(false)
{
  auto  anchorStep = _settingsManager->getOrAddSetting(
    "Plugin Interface/Anchor step", QList<QVariant> { 100000, 100000 })
                     .toList();

  _anchorStepX = anchorStep[0].toDouble();
  _anchorStepY = anchorStep[1].toDouble();
}

MPluginInterface::~MPluginInterface(void)
{
}

void  MPluginInterface::init()
{
	_activated = new bool;
	_activated = false;

	_pluginRoot = new osg::Group;
	_pluginRoot->setName(_pluginName.toLocal8Bit().toStdString());

	if (_pluginCategory == "Data")
  {
    _dataRoot->addChild(_pluginRoot);
  }
  else
  {
    _drawRoot->addChild(_pluginRoot);
  }

	_style = getDefaultStyle();

  _currentWorldPos = { 0, 0, 0 };
  updateAnchorPoint();

  auto  clickSetting = _settingsManager->getOrAddSetting("Plugin Interface/Draw click limit", 0.5);
  clickLimit = clickSetting.toDouble();
}

void  MPluginInterface::loadContextMenu(QMenu *contextMenu, QTreeWidgetItem *selectedItem)
{
}

void  MPluginInterface::setDefaultAction(QAction *action)
{
	_defaultAction = action;
}

QString  MPluginInterface::getPluginName()
{
	return _pluginName;
}

QString  MPluginInterface::getPluginGroup()
{
	return _pluginCategory;
}

void  MPluginInterface::updateAnchorPoint()
{
  _currentAnchor    = getNearestAnchorPoint();
  _anchoredOffset   = _currentAnchor->getPosition();
  _anchoredWorldPos = _currentWorldPos - _anchoredOffset;
}

osg::PositionAttitudeTransform * MPluginInterface::getNearestAnchorPoint(const osg::Vec3 &point)
{
	// Find the anchor point
  unsigned  x = point.x() / _anchorStepX;
  unsigned  y = point.y() / _anchorStepY;

	if (anchorPoints.find(x) != anchorPoints.end())
	{
    auto  itr = anchorPoints[x];

		if (itr.find(y) != itr.end())
		{
			return itr[y];
		}
	}
	else
	{
		anchorPoints[x] = QMap<unsigned, osg::ref_ptr<osg::PositionAttitudeTransform>>();
	}

	// If not exist yet, generate one
  osg::ref_ptr<osg::PositionAttitudeTransform>  anchor = new osg::PositionAttitudeTransform;
  anchor->setPosition({ x *_anchorStepX + _anchorStepX / 2, y *_anchorStepY + _anchorStepY / 2, 0 });

	_pluginRoot->addChild(anchor);
	anchorPoints[x][y] = anchor;

	return anchor;
}

osg::ref_ptr<osg::Vec3Array>  MPluginInterface::anchorArray(osg::ref_ptr<osg::Vec3Array> worldSpaceArray)
{
  osg::ref_ptr<osg::Vec3Array>  anchoredArray = new osg::Vec3Array(worldSpaceArray->size());

  for (unsigned i = 0; i < anchoredArray->size(); i++)
  {
    anchoredArray->at(i) = worldSpaceArray->at(i) - _anchoredOffset;
  }

  return anchoredArray;
}

bool  MPluginInterface::handle(const osgGA::GUIEventAdapter &ea, osgGA::GUIActionAdapter &aa)
{
	// Perform only default operation when plugin is not activated
	if (_activated == false)
	{
    if ((ea.getEventType() == osgGA::GUIEventAdapter::PUSH) && (ea.getButton() == osgGA::GUIEventAdapter::LEFT_MOUSE_BUTTON))
    {
			defaultOperation(ea, aa);
    }

    return false;
	}

	// Get view and event adaptor
  osgViewer::View *view = dynamic_cast<osgViewer::View *>(&aa);

	if (!view)
  {
    return false;
  }

  _currentView = view;
  _currentEA   = &ea;

  // Update the position used in drawing
  if (_isDrawing)
  {
    _anchoredWorldPos = _currentWorldPos - _anchoredOffset;
  }

	// Handle inputs (non-blocking)
	switch (ea.getEventType())
	{
  case (osgGA::GUIEventAdapter::DOUBLECLICK):
		lastClicked = ea.getTime();

		return false;
  case (osgGA::GUIEventAdapter::PUSH):

		switch (ea.getButton())
		{
    case (osgGA::GUIEventAdapter::LEFT_MOUSE_BUTTON):
			onLeftButton();

			return false;
    case (osgGA::GUIEventAdapter::RIGHT_MOUSE_BUTTON):
			onRightButton();

			return false;
		default:

			return false;
		}

  case (osgGA::GUIEventAdapter::RELEASE):

		if (ea.getButton() == osgGA::GUIEventAdapter::LEFT_MOUSE_BUTTON)
    {
      onReleaseButton();
    }

    if (ea.getTime() - lastClicked < clickLimit)
    {
      onDoubleClick();
    }

    return false;
  case (osgGA::GUIEventAdapter::MOVE):
		onMouseMove();

		return false;
  case (osgGA::GUIEventAdapter::KEYDOWN):

    switch (ea.getKey())
    {
    case (osgGA::GUIEventAdapter::KEY_Up):
      onArrowKeyUp();

      return false;
    case (osgGA::GUIEventAdapter::KEY_Down):
      onArrowKeyDown();

      return false;
    default:

      return false;
    }

	default:

		return false;
	}
}

QVariant  MPluginInterface::getOrAddPluginSettings(const QString& key, const QVariant &value)
{
  return _settingsManager->getOrAddSetting(_pluginName + "/" + key, value);
}

QVariant  MPluginInterface::setPluginSettings(const QString& key, const QVariant &value)
{
  QSettings().setValue(_pluginName + "/" + key, value);

  return value;
}

osg::ref_ptr<osg::Geometry>  MPluginInterface::createPointGeode(const osg::Vec3 &pos, const osg::Vec3 &norm)
{
	osg::Geometry *geom = new osg::Geometry();

  osg::ref_ptr<osg::Vec3Array>  vertex = new osg::Vec3Array();
	vertex->push_back(pos);
	geom->setVertexArray(vertex.get());

  osg::ref_ptr<osg::Vec4Array>  color = new osg::Vec4Array();
	color->push_back(_style.pointColor);
	geom->setColorArray(color, osg::Array::BIND_OVERALL);

  osg::ref_ptr<osg::Vec3Array>  norms = new osg::Vec3Array();
	norms->push_back(norm);
	geom->setNormalArray(norms, osg::Array::BIND_OVERALL);

	geom->getOrCreateStateSet()->setAttribute(_style.pointSize.get(), osg::StateAttribute::ON);
	geom->getOrCreateStateSet()->setMode(GL_POINT_SMOOTH, osg::StateAttribute::ON);
	geom->addPrimitiveSet(new osg::DrawArrays(osg::PrimitiveSet::POINTS, 0, 1));

	geom->setName("point");

	return geom;
}

osg::ref_ptr<osgText::Text>  MPluginInterface::createTextGeode(std::string &txt, osg::Vec3 position)
{
  osg::ref_ptr<osgText::Text>  text = new osgText::Text;

	text->setFont(_style.font);
	text->setFontResolution(128, 128);
	text->setCharacterSize(_style.textSize);
	text->setColor(_style.textColor);
	text->setLineSpacing(0.25);

	text->setAxisAlignment(osgText::TextBase::SCREEN);
	text->setCharacterSizeMode(osgText::Text::OBJECT_COORDS_WITH_MAXIMUM_SCREEN_SIZE_CAPPED_BY_FONT_HEIGHT);
	text->setCharacterSizeMode(osgText::Text::SCREEN_COORDS);
	text->setAlignment(osgText::TextBase::CENTER_BASE_LINE);

  position.z() += _style.textFloating;
	text->setPosition(position);

	QTextCodec *codec = QTextCodec::codecForName("GB2312");
  QString     ss    = codec->toUnicode(txt.c_str());

	text->setText(ss.toLocal8Bit().toStdString(), osgText::String::ENCODING_UTF8);

	return text.release();
}

void  MPluginInterface::registerMutexAction(QAction *action)
{
	if (!_mutexActions)
	{
		// The default behaviour of QActionGroup always requires one action to be enabled
		// So we set exclusive to fales in order to allow none-action-enabled case
		_mutexActions = new QActionGroup(_mainWindow);
		_mutexActions->setExclusive(false);
		connect(_mutexActions, &QActionGroup::triggered, &MPluginInterface::uncheckMutexActions);
	}

	_mutexActions->addAction(action);
}

void  MPluginInterface::uncheckMutexActions(QAction *action)
{
	// When the triggered action is going to be toggled off, toggle on the default action
  if (!action->isChecked() && (_defaultAction != action) && (_defaultAction != NULL))
	{
		_defaultAction->toggle();

		return;
	}

	// Toggle off any other enabled actions
  for (QAction *others : _mutexActions->actions())
	{
    if (others->isChecked() && (others != action))
    {
			others->toggle();
    }
  }
}

void  MPluginInterface::recordCurrent(const QString& parent)
{
  QString  nodeName;

  if (_currentDrawNode->getName().empty())
  {
    nodeName = _pluginName;
  }
  else
  {
    nodeName = QString::fromStdString(_currentDrawNode->getName());
  }

  emit  recordData(_currentDrawNode, nodeName, parent.isEmpty() ? _pluginName : parent);
}

void  MPluginInterface::recordNode(osg::Node *node, const QString& name, const QString& parent)
{
  QString  nodeName = name.isEmpty() ? QString::fromStdString(node->getName()) : name;

  if (nodeName.isEmpty())
  {
    nodeName = _pluginName;
  }

  emit  recordData(node, nodeName, parent.isEmpty() ? _pluginName : parent);
}

void MPluginInterface::recordLayer(osgEarth::Layer * layer, const QString& name, const QString& parent)
{
  emit  recordData(layer, name.isEmpty() ? QString::fromStdString(layer->getName()) : name, parent.isEmpty() ? _pluginName : parent);
}

void  MPluginInterface::toggle(bool checked)
{
	_activated = checked;
}

MPluginInterface::StyleConfig  MPluginInterface::getDefaultStyle()
{
	// Get plugin-specific style
  auto  localStyle = getOrAddPluginSettings("Draw style").toMap();

  // Init global default style
  auto  globalStyle = QSettings().value("Plugin Interface/Draw style").toMap();

	if (globalStyle.isEmpty())
	{
    globalStyle["Line color"]    = QColor(255, 255, 0);
    globalStyle["Point color"]   = QColor(0, 255, 0);
    globalStyle["Text color"]    = QColor(255, 0, 0);
    globalStyle["Fill color"]    = QColor(174, 234, 224);
    globalStyle["Line width"]    = 4.0f;
    globalStyle["Point size"]    = 6.0f;
    globalStyle["Font path"]     = "Resources/fonts/arial.ttf";
    globalStyle["Font size"]     = 28.0f;
    globalStyle["Text floating"] = 5.0f;
    QSettings().setValue("Plugin Interface/Draw style", globalStyle);
	}

  // Plugin style may be incomplete, use global style as fallback
  for (auto &key : globalStyle.keys())
  {
    if (localStyle.find(key) == localStyle.end())
    {
      localStyle[key] = globalStyle[key];
    }
  }

	// Get style as locally compatible format
  StyleConfig  styleConfig;
  styleConfig.lineColor  = colorToVec(localStyle["Line color"].value<QColor>());
  styleConfig.fillColor  = colorToVec(localStyle["Fill color"].value<QColor>());
	styleConfig.pointColor = colorToVec(localStyle["Point color"].value<QColor>());
  styleConfig.textColor  = colorToVec(localStyle["Text color"].value<QColor>());
	styleConfig.lineWidth->setWidth(localStyle["Line width"].toFloat());
	styleConfig.pointSize->setSize(localStyle["Point size"].toFloat());
  styleConfig.textSize     = localStyle["Font size"].toFloat();
  styleConfig.textFloating = localStyle["Text floating"].toFloat();
  styleConfig.font         = osgText::readFontFile(localStyle["Font path"].toString().toLocal8Bit().toStdString());
  if (styleConfig.font)
  {
	  styleConfig.font->setMinFilterHint(osg::Texture::LINEAR);
	  styleConfig.font->setMagFilterHint(osg::Texture::LINEAR);
  }
	

	return styleConfig;
}

MPluginInterface::StyleConfig::StyleConfig()
{
	lineWidth = new osg::LineWidth;
	pointSize = new osg::Point;
  font      = new osgText::Font;
}

void  MPluginInterface::beginDrawing()
{
  updateAnchorPoint();
  _isDrawing = true;
}

void  MPluginInterface::endDrawing()
{
  _isDrawing = false;
}
