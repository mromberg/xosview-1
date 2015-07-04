//
//  Copyright (c) 1994, 1995, 2002, 2006, 2015
//  by Mike Romberg ( mike-romberg@comcast.net )
//
//  This file may be distributed under terms of the GPL
//
#include "xosview.h"
#include "meter.h"
#include "x11font.h"
#include "clopt.h"
#ifdef HAVE_XFT
#include "xftfont.h"
#endif
#include "MeterMaker.h"
#include "strutil.h"
#if (defined(XOSVIEW_NETBSD) || defined(XOSVIEW_FREEBSD) || defined(XOSVIEW_OPENBSD))
#include "kernel.h"
#endif

#include <algorithm>

#include <unistd.h>



static const char * const VersionString = "xosview version: " PACKAGE_VERSION;
static const char NAME[] = "xosview@";


double XOSView::MAX_SAMPLES_PER_SECOND = 10;

//
XOSView::XOSView(void)
    : XWin(), _xrm(0),
      caption_(false), legend_(false), usedlabels_(false),
      xoff_(0), yoff_(0),
      hmargin_(0), vmargin_(0), vspacing_(0),
      sleeptime_(1), usleeptime_(1000),
      expose_flag_(false), exposed_once_flag_(false),
      _isvisible(false), _ispartiallyvisible(false) {
}

int XOSView::findx(XOSVFont &font){
    if ( legend_ ){
        if ( !usedlabels_ )
            return font.textWidth( "XXXXXXXXXXXXXXXXXXXXXXXX" );
        else
            return font.textWidth( "XXXXXXXXXXXXXXXXXXXXXXXXXXXXX" );
    }
    return 80;
}

int XOSView::findy(XOSVFont &font){
    if ( legend_ )
        return 10 + font.textHeight() * _meters.size() * ( caption_ ? 2 : 1 );

    return 15 * _meters.size();
}

void XOSView::figureSize(void) {
    std::string fname = getResource("font");
    logDebug << "Font name: " << fname << std::endl;
#ifdef HAVE_XFT
    X11ftFont font(display(), fname);
#else
    X11Font font(display(), fname);
#endif
    if (!font)
        logFatal << "Could not load font: " << fname << std::endl;

    if ( legend_ ){
        if ( !usedlabels_ )
            xoff_ = font.textWidth( "XXXXX" );
        else
            xoff_ = font.textWidth( "XXXXXXXXX" );

        yoff_ = caption_ ? font.textHeight() + font.textHeight() / 4 : 0;
    }
    static bool firsttime = true;
    if (firsttime) {
        firsttime = false;
        width(findx(font));
        height(findy(font));
    }
}

void XOSView::checkMeterResources( void ){
    for (size_t i = 0 ; i < _meters.size() ; i++)
        _meters[i]->checkResources();
}

int XOSView::newypos( void ){
    return 15 + 25 * _meters.size();
}

void XOSView::dolegends( void ){
    for (size_t i = 0 ; i < _meters.size() ; i++) {
        _meters[i]->docaptions(caption_);
        _meters[i]->dolegends(legend_);
        _meters[i]->dousedlegends(usedlabels_);
    }
}

std::string XOSView::winname( void ){
    char host[100];
    std::string hname("unknown");
    if (gethostname( host, 99 )) {
        logProblem << "gethostname() failed" << std::endl;
    }
    else {
        host[99] = '\0';  // POSIX.1-2001 says truncated names not terminated
        hname = std::string(host);
    }
    std::string name = std::string(NAME) + hname;
    return getResourceOrUseDefault("title", name);
}


void  XOSView::resize( void ){
    int spacing = vspacing_+1;
    int topmargin = vmargin_;
    int rightmargin = hmargin_;
    int newwidth = width() - xoff_ - rightmargin;
    size_t nmeters = _meters.size();
    nmeters = nmeters ? nmeters : 1; // don't divide by zero
    int newheight =
        (height() -
          (topmargin + topmargin + (nmeters-1)*spacing + nmeters*yoff_)
            ) / nmeters;
    newheight = (newheight >= 2) ? newheight : 2;

    for (size_t i = 0 ; i < _meters.size() ; i++) {
        _meters[i]->resize(xoff_,
          topmargin + (i + 1) * yoff_ + i * (newheight+spacing),
          newwidth, newheight);
    }
}


XOSView::~XOSView( void ){
    logDebug << "deleting " << _meters.size() << " meters..." << std::endl;
    for (size_t i = 0 ; i < _meters.size() ; i++)
        delete _meters[i];
    _meters.resize(0);
    delete _xrm;
}

void XOSView::reallydraw( void ){
    logDebug << "Doing draw." << std::endl;
    g().clear();

    for (size_t i = 0 ; i < _meters.size() ; i++)
        _meters[i]->draw(g());

    g().flush();

    expose_flag_ = false;
}

void XOSView::draw ( void ) {
    if (hasBeenExposedAtLeastOnce() && isAtLeastPartiallyVisible())
        reallydraw();
    else {
        if (!hasBeenExposedAtLeastOnce()) {
            logDebug << "Skipping draw:  not yet exposed." << std::endl;
        } else if (!isAtLeastPartiallyVisible()) {
            logDebug << "Skipping draw:  not visible." << std::endl;
        }
    }
}

void XOSView::loadConfiguration(int argc, char **argv) {
    //...............................................
    // Command line options
    //...............................................

    util::CLOpts clopts(argc, argv);
    // No one touches (or even looks at) argc or argv
    // beyond this point without an EXCELLENT reason.
    //!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*
    setCommandLineArgs(clopts);
    clopts.parse(); // will exit() if fails.

    if (clopts.isTrue("help")) {
        std::cout << versionStr() << "\n\n" << clopts.useage() << std::endl;
        exit(0);
    }
    if (clopts.isTrue("version")) {
        std::cout << versionStr() << std::endl;
        exit(0);
    }

    //...............................................
    // X resources
    //...............................................
    _xrm = new Xrm("xosview", clopts.value("name", "xosview"));

    //  Open the Display and load the X resources
    setDisplayName(clopts.value("displayName", ""));
    openDisplay();
    _xrm->loadResources(display());

    // Now load any resouce files specified on the command line
    const std::vector<std::string> &cfiles = clopts.values("configFile");
    for (size_t i = 0 ; i < cfiles.size() ; i++)
        _xrm->loadResources(cfiles[i]);

    // load all of the command line options into the
    // resouce database.  First the ones speced by -xrm
    const std::vector<std::string> &xrml = clopts.values("xrm");
    for (size_t i = 0 ; i < xrml.size() ; i++) {
        _xrm->putResource(xrml[i]);
        logDebug << "ADD: " << xrml[i] << std::endl;
    }

    // Now all the rest that are set by the user.
    // defaults delt with by getResourceOrUseDefault()
    const std::vector<util::CLOpt> &opts = clopts.opts();
    for (size_t i = 0 ; i < opts.size() ; i++)
        if (opts[i].name() != "xrm" && !opts[i].missing()) {
            std::string rname("." + instanceName() + "*" +opts[i].name());
            _xrm->putResource(rname, opts[i].value());
            logDebug << "ADD: "
                     << rname << " : " << opts[i].value()
                     << std::endl;
        }

    // The window manager will later wanna know the command line
    // arguments.  Since this may be used to restore a session, we
    // will save them here in a resource.
    std::string command;
    for (int i = 0 ; i < argc ; i++)
        command += std::string(" ") + argv[i];
    _xrm->putResource("." + instanceName() + "*command", command);

    //---------------------------------------------------
    // No use of clopts beyond this point.  It is all in
    // the resource database now.  Example immediately follows...
    //---------------------------------------------------
    if (isResourceTrue("xrmdump")) {
        _xrm->dump(std::cout);
        exit(0);
    }
}

// Think about having run() return an int
// so it works just like main.  failures
// in run() return an exit status for main
// this way the destructors run.
void XOSView::run(int argc, char **argv){

    loadConfiguration(argc, argv);
    setEvents();           //  set up the X events
    setSleepTime();
    loadResources();       // initialize from our resources
    createMeters();        // add in the meters
    figureSize();          // calculate size using number of meters
    createWindow();        // Graphics should now be up (so can alloc colors)
    checkMeterResources(); // Have the meters re-check the resources.
    title(winname());      // Now that the window exists set the title
    iconname(winname());   // and the icon name
    dolegends();
    resize();

    loop();                // enter event loop
}

void XOSView::loop(void) {
    int counter = 0;

    while( !done() ){
        checkevent();
        if (done()) {
            logDebug << "checkevent() set done" << std::endl;
            break; // XEvents can set this
        }

        if (_isvisible){
            for (size_t i = 0 ; i < _meters.size() ; i++) {
                if ( _meters[i]->requestevent() )
                    _meters[i]->checkevent();
            }

            g().flush();
        }
#ifdef HAVE_USLEEP
        /*  First, sleep for the proper integral number of seconds --
         *  usleep only deals with times less than 1 sec.  */
        if (sleeptime_)
            sleep((unsigned int)sleeptime_);
        if (usleeptime_)
            usleep( (unsigned int)usleeptime_);
#else
        usleep_via_select ( usleeptime_ );
#endif
        counter = (counter + 1) % 5;
    }
    logDebug << "leaging run()..." << std::endl;
}

void XOSView::usleep_via_select( unsigned long usec ){
    struct timeval time;

    time.tv_sec = (int)(usec / 1000000);
    time.tv_usec = usec - time.tv_sec * 1000000;

    select( 0, 0, 0, 0, &time );
}

void XOSView::keyPressEvent( XKeyEvent &event ){
    char c = 0;
    KeySym key;

    XLookupString( &event, &c, 1, &key, NULL );

    if ( (c == 'q') || (c == 'Q') )
        done(true);
}


void XOSView::exposeEvent( XExposeEvent &event ) {
    _isvisible = true;
    if ( event.count == 0 ) {
        expose_flag_ = true;
        exposed_once_flag_ = true;
        draw();
    }
    logDebug << "Got expose event." << std::endl;
    if (!exposed_once_flag_) {
        exposed_once_flag_ = 1;
        draw();
    }
}

void XOSView::resizeEvent( XEvent &e ) {
    g().resize(e.xconfigure.width, e.xconfigure.height);
    resize();
    expose_flag_ = true;
    exposed_once_flag_ = true;
    draw();
}


void XOSView::visibilityEvent( XVisibilityEvent &event ){
    _ispartiallyvisible = false;
    if (event.state == VisibilityPartiallyObscured){
        _ispartiallyvisible = true;
    }

    if (event.state == VisibilityFullyObscured){
        _isvisible = false;
    }
    else {
        _isvisible = true;
    }
    logDebug << "Got visibility event; " << _ispartiallyvisible
             << " and " << _isvisible << std::endl;
}

void XOSView::unmapEvent( XUnmapEvent & ){
    _isvisible = false;
}

double XOSView::maxSampRate(void) {
    return MAX_SAMPLES_PER_SECOND;
}

void XOSView::setSleepTime(void) {
    MAX_SAMPLES_PER_SECOND = util::stof(getResource("samplesPerSec"));
    if (!MAX_SAMPLES_PER_SECOND)
        MAX_SAMPLES_PER_SECOND = 10;

    usleeptime_ = (unsigned long) (1000000/MAX_SAMPLES_PER_SECOND);
    if (usleeptime_ >= 1000000) {
        /*  The syscall usleep() only takes times less than 1 sec, so
         *  split into a sleep time and a usleep time if needed.  */
        sleeptime_ = usleeptime_ / 1000000;
        usleeptime_ = usleeptime_ % 1000000;
    } else {
        sleeptime_ = 0;
    }
#if (defined(XOSVIEW_NETBSD) || defined(XOSVIEW_FREEBSD) || defined(XOSVIEW_OPENBSD))
    BSDInit();	/*  Needs to be done before processing of -N option.  */
#endif
}

void XOSView::loadResources(void) {
    hmargin_  = util::stoi(getResource("horizontalMargin"));
    vmargin_  = util::stoi(getResource("verticalMargin"));
    vspacing_ = util::stoi(getResource("verticalSpacing"));
    hmargin_  = std::max(0, hmargin_);
    vmargin_  = std::max(0, vmargin_);
    vspacing_ = std::max(0, vspacing_);

#if (defined(XOSVIEW_NETBSD) || defined(XOSVIEW_FREEBSD) || defined(XOSVIEW_OPENBSD))
    opt kname = _xrm->getResource("kernelName");
    if (opt.first)
        SetKernelName(opt.second.c_str());
#endif

    xoff_ = hmargin_;
    yoff_ = 0;
    appName("xosview");
    _isvisible = false;
    _ispartiallyvisible = false;
    expose_flag_ = true;
    exposed_once_flag_ = false;

    //  Set 'off' value.  This is not necessarily a default value --
    //    the value in the defaultXResourceString is the default value.
    usedlabels_ = legend_ = caption_ = false;

    // use captions
    if ( isResourceTrue("captions") )
        caption_ = 1;

    // use labels
    if ( isResourceTrue("labels") )
        legend_ = 1;

    // use "free" labels
    if ( isResourceTrue("usedlabels") )
        usedlabels_ = 1;
}

void XOSView::setEvents(void) {
    XWin::setEvents();

    addEvent( new Event( this, ConfigureNotify,
        (EventCallBack)&XOSView::resizeEvent ) );
    addEvent( new Event( this, Expose,
        (EventCallBack)&XOSView::exposeEvent ) );
    addEvent( new Event( this, KeyPress,
        (EventCallBack)&XOSView::keyPressEvent ) );
    addEvent( new Event( this, VisibilityNotify,
        (EventCallBack)&XOSView::visibilityEvent ) );
    addEvent( new Event( this, UnmapNotify,
        (EventCallBack)&XOSView::unmapEvent ) );
}

void XOSView::createMeters(void) {
    MeterMaker mm(this);
    mm.makeMeters();
    for (int i = 1 ; i <= mm.n() ; i++)
        _meters.push_back(mm[i]);

    if (_meters.size() == 0)
        logProblem << "No meters were enabled." << std::endl;
}

bool XOSView::isResourceTrue( const std::string &name ) {
    Xrm::opt val = _xrm->getResource(name);
    if (!val.first)
        return false;

    return val.second == "True";
}

std::string XOSView::getResourceOrUseDefault( const std::string &name,
  const std::string &defaultVal ){

    Xrm::opt retval = _xrm->getResource (name);
    if (retval.first)
        return retval.second;

    return defaultVal;
}

std::string XOSView::getResource( const std::string &name ){
    Xrm::opt retval = _xrm->getResource (name);
    if (retval.first)
        return retval.second;
    else {
        logFatal << "Couldn't find '" << name
                 << "' resource in the resource database!\n";
        /*  Some compilers aren't smart enough to know that exit() exits.  */
        return '\0';
    }
}

void XOSView::dumpResources( std::ostream &os ){
    _xrm->dump(os);
}

std::string XOSView::versionStr(void) const {
    return VersionString;
}

void XOSView::setCommandLineArgs(util::CLOpts &o) {

    //------------------------------------------------------
    // Define ALL of the command line options here.
    //
    // Note.  Every command line option that is set by the user
    // will be loaded into the Xrm database using the name
    // given here.  So, you can lookup these options anywhere
    // getResource() is available.
    //------------------------------------------------------
    o.add("help",
      "-h", "--help",
      "Display this message and exit.");
    o.add("version",
      "-v", "--version",
      "Display version information and exit.");

    // General "common" X resouces
    //-----------------------------------------
    o.add("displayName",
      "-display", "--display", "name",
      "The name of the X display to connect to.");
    o.add("title",
      "-title", "--title", "title",
      "The title to use.");
    o.add("geometry",
      "-g", "-geometry", "geometry",
      "The X geometry string to use.");
    o.add("foreground",
      "-fg", "--foreground", "color",
      "The color to use for the foreground.");
    o.add("background",
      "-bg", "--background", "color",
      "The color to use for the background.");
    o.add("font",
      "-fn", "-font", "fontName",
      "The name of the font to use.");
    o.add("iconic",
      "-iconic", "--iconic",
      "Request to start in an iconic state.");
    o.add("name",
      "-name", "--name", "name",
      "The X resource instance name to use.");
    o.add("xrm",
      "-x", "-xrm",
      "spec",
      "Set an X resource using the supplied spec.  For example: "
      "-x '*background: red' would set the background to red.");

    // Xosview specific options
    //----------------------------------------------------
    o.add("configFile",
      "-c", "--config", "fileName",
      "Load an XResource file containing overrides.");
    o.add("xrmdump",
      "-xrmd", "--xrm-dump",
      "Dump the X resouces seen by xosview to stdout and exit.");

    //-----------------------------------------------------
    // No other options that override X resources are needed
    // as they can be easily done with one or more of the
    // methods above.  We don't need to support as many options
    // as the UNIX ls command.
    //-----------------------------------------------------

    // Near as I can tell this was pretty undocumented.  I only found it
    // mentioned in passing in the README.netbsd file.  There was
    // code in here for it.  So, I'll attempt to keep the functionality.
    // The actual command line syntax may be different.  UNTESTED.
#if (defined(XOSVIEW_NETBSD) || defined(XOSVIEW_FREEBSD) || defined(XOSVIEW_OPENBSD))
    o.add("kernelName",
      "-N", "--kernel-name", "name",
      "Sets the kernel name for BSD variants.");
#endif
}
