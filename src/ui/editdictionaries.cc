/* This file is (c) 2008-2012 Konstantin Isakov <ikm@goldendict.org>
 * Part of GoldenDict. Licensed under GPLv3 or later, see the LICENSE file */

#include "editdictionaries.hh"
#include "dict/loaddictionaries.hh"
#include "help.hh"
#include <QMessageBox>

using std::vector;

EditDictionaries::EditDictionaries( QWidget * parent, Config::Class & cfg_,
                                    vector< sptr< Dictionary::Class > > & dictionaries_,
                                    Instances::Groups & groupInstances_,
                                    QNetworkAccessManager & dictNetMgr_ ):
  QDialog( parent, Qt::WindowSystemMenuHint | Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint ),
  cfg( cfg_ ),
  dictionaries( dictionaries_ ),
  groupInstances( groupInstances_ ),
  dictNetMgr( dictNetMgr_ ),
  origCfg( cfg ),
  sources( this, cfg ),
  orderAndProps( new OrderAndProps( this, cfg.dictionaryOrder, cfg.inactiveDictionaries,
                                    dictionaries ) ),
  groups( new Groups( this, dictionaries, cfg.groups, orderAndProps->getCurrentDictionaryOrder() ) ),
  dictionariesChanged( false ),
  groupsChanged( false ),
  lastCurrentTab( 0 ),
  helpAction( this )
{
  // Some groups may have contained links to non-existnent dictionaries. We
  // would like to preserve them if no edits were done. To that end, we save
  // the initial group readings so that if no edits were really done, we won't
  // be changing groups.
  origCfg.groups = groups->getGroups();
  origCfg.dictionaryOrder = orderAndProps->getCurrentDictionaryOrder();
  origCfg.inactiveDictionaries = orderAndProps->getCurrentInactiveDictionaries();

  ui.setupUi( this );

  setWindowIcon( QIcon(":/icons/dictionary.svg") );

  ui.tabs->clear();

  ui.tabs->addTab( &sources, QIcon(":/icons/sources.png"), tr( "&Sources" ) );
  ui.tabs->addTab( orderAndProps, QIcon(":/icons/book.svg"), tr( "&Dictionaries" ) );
  ui.tabs->addTab( groups.get(), QIcon(":/icons/bookcase.svg"), tr( "&Groups" ) );

  connect( ui.buttons, &QDialogButtonBox::clicked, this, &EditDictionaries::buttonBoxClicked );

  connect( &sources, &Sources::rescan, this, &EditDictionaries::rescanSources );

  connect( groups.get(), &Groups::showDictionaryInfo, this, &EditDictionaries::showDictionaryInfo );

  connect( orderAndProps.data(),
    &OrderAndProps::showDictionaryHeadwords,
    this,
    &EditDictionaries::showDictionaryHeadwords );

  helpAction.setShortcut( QKeySequence( "F1" ) );
  helpAction.setShortcutContext( Qt::WidgetWithChildrenShortcut );

  connect( &helpAction, &QAction::triggered, [ this ]() {
    if ( ui.tabs->currentWidget() == this->groups.get() ) {
      Help::openHelpWebpage( Help::section::manage_groups );
    }
    else {
      Help::openHelpWebpage( Help::section::manage_sources );
    }
  } );
  connect( ui.buttons, &QDialogButtonBox::helpRequested, &helpAction, &QAction::trigger );

  addAction( &helpAction );

}

void EditDictionaries::editGroup( unsigned id )
{
  ui.tabs->setTabVisible( 0, false );

  if ( id == Instances::Group::AllGroupId )
  {
    ui.tabs->setCurrentIndex( 1 );
  }
  else
  {
    ui.tabs->setCurrentIndex( 2 );
    groups->editGroup( id );
  }
}

void EditDictionaries::save( bool rebuildGroups )
{
  Config::Groups newGroups = groups->getGroups();
  Config::Group newOrder = orderAndProps->getCurrentDictionaryOrder();
  Config::Group newInactive = orderAndProps->getCurrentInactiveDictionaries();

  if( isSourcesChanged() )
    acceptChangedSources( rebuildGroups );

  if ( origCfg.groups != newGroups || origCfg.dictionaryOrder != newOrder ||
       origCfg.inactiveDictionaries != newInactive )
  {
    groupsChanged = true;
    cfg.groups = newGroups;
    cfg.dictionaryOrder = newOrder;
    cfg.inactiveDictionaries = newInactive;
  }
}

void EditDictionaries::accept()
{
  save();
  QDialog::accept();
}

void EditDictionaries::on_tabs_currentChanged( int index )
{
  if ( index == -1 || !isVisible() )
    return; // Sent upon the construction/destruction

  if ( !lastCurrentTab && index )
  {
    // We're switching away from the Sources tab -- if its contents were
    // changed, we need to either apply or reject now.

    if ( isSourcesChanged() )
    {
      ui.tabs->setCurrentIndex( 0 );

      QMessageBox question( QMessageBox::Question, tr( "Sources changed" ),
                            tr( "Some sources were changed. Would you like to accept the changes?" ),
                            QMessageBox::NoButton, this );

      QPushButton * accept = question.addButton( tr( "Accept" ), QMessageBox::AcceptRole );

      question.addButton( tr( "Cancel" ), QMessageBox::RejectRole );

      question.exec();

      if ( question.clickedButton() == accept )
      {
        acceptChangedSources( true );
        
        lastCurrentTab = index;
        ui.tabs->setCurrentIndex( index );
      }
      else
      {
        // Prevent tab from switching
        lastCurrentTab = 0;
        return;
      }
    }
  }
  else
  if ( lastCurrentTab == 1 && index != 1 )
  {
    // When switching from the dictionary order, we need to propagate any
    // changes to the groups.
    groups->updateDictionaryOrder( orderAndProps->getCurrentDictionaryOrder() );
  }

  lastCurrentTab = index;
}

void EditDictionaries::rescanSources()
{
  acceptChangedSources( true );
}

void EditDictionaries::buttonBoxClicked( QAbstractButton * button )
{
  if (ui.buttons->buttonRole(button) == QDialogButtonBox::ApplyRole) {
    save( true );
  }
}

bool EditDictionaries::isSourcesChanged() const
{
  return sources.getPaths() != cfg.paths ||
         sources.getSoundDirs() != cfg.soundDirs ||
         sources.getHunspell() != cfg.hunspell ||
         sources.getTransliteration() != cfg.transliteration ||
         sources.getLingua() != cfg.lingua ||
         sources.getForvo() != cfg.forvo ||
         sources.getMediaWikis() != cfg.mediawikis ||
         sources.getWebSites() != cfg.webSites ||
         sources.getDictServers() != cfg.dictServers ||
         sources.getPrograms() != cfg.programs ||
         sources.getVoiceEngines() != cfg.voiceEngines;
}

void EditDictionaries::acceptChangedSources( bool rebuildGroups )
{
  dictionariesChanged = true;

  Config::Groups savedGroups = groups->getGroups();
  Config::Group savedOrder = orderAndProps->getCurrentDictionaryOrder();
  Config::Group savedInactive = orderAndProps->getCurrentInactiveDictionaries();

  cfg.paths = sources.getPaths();
  cfg.soundDirs = sources.getSoundDirs();
  cfg.hunspell = sources.getHunspell();
  cfg.transliteration = sources.getTransliteration();
  cfg.lingua = sources.getLingua();
  cfg.forvo = sources.getForvo();
  cfg.mediawikis = sources.getMediaWikis();
  cfg.webSites = sources.getWebSites();
  cfg.dictServers = sources.getDictServers();
  cfg.programs = sources.getPrograms();
  cfg.voiceEngines = sources.getVoiceEngines();

  groupInstances.clear(); // Those hold pointers to dictionaries, we need to
                          // free them.
  ui.tabs->setUpdatesEnabled( false );

  groups.reset();
  orderAndProps.clear();

  loadDictionaries( this, true, cfg, dictionaries, dictNetMgr );

  // If no changes to groups were made, update the original data
  bool noGroupEdits = ( origCfg.groups == savedGroups );

  if ( noGroupEdits )
    savedGroups = cfg.groups;

  Instances::updateNames( savedGroups, dictionaries );

  bool noOrderEdits = ( origCfg.dictionaryOrder == savedOrder );

  if ( noOrderEdits )
    savedOrder = cfg.dictionaryOrder;

  Instances::updateNames( savedOrder, dictionaries );

  bool noInactiveEdits = ( origCfg.inactiveDictionaries == savedInactive );

  if ( noInactiveEdits )
    savedInactive  = cfg.inactiveDictionaries;

  Instances::updateNames( savedInactive, dictionaries );

  if ( rebuildGroups )
  {
    orderAndProps = new OrderAndProps( this, savedOrder, savedInactive, dictionaries );
    groups =  std::make_shared<Groups>( this, dictionaries, savedGroups, orderAndProps->getCurrentDictionaryOrder() );

    ui.tabs->removeTab( 1 );
    ui.tabs->removeTab( 1 );
    ui.tabs->insertTab( 1, orderAndProps, QIcon(":/icons/book.svg"), tr( "&Dictionaries" ) );

    ui.tabs->insertTab( 2, groups.get(), QIcon(":/icons/bookcase.svg"), tr( "&Groups" ) );


    if ( noGroupEdits )
      origCfg.groups = groups->getGroups();

    if ( noOrderEdits )
      origCfg.dictionaryOrder = orderAndProps->getCurrentDictionaryOrder();

    if ( noInactiveEdits )
      origCfg.inactiveDictionaries = orderAndProps->getCurrentInactiveDictionaries();
  }
  ui.tabs->setUpdatesEnabled( true );

}
