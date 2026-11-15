// Timer support (port of RobotRaconteurNET/TimerNET.i without C# modifiers)
%inline {
struct WallTimer_initstruct
{
	RobotRaconteur::AsyncTimerEventReturnDirector* handler;
	int32_t id;
};
}

%include "Timer.i"

%extend RobotRaconteur::WallTimer
{
// cSpell: ignore sphandler
WallTimer(const boost::posix_time::time_duration& period, bool oneshot, const boost::shared_ptr<RobotRaconteur::RobotRaconteurNode>& node, WallTimer_initstruct init )
{
	boost::shared_ptr<AsyncTimerEventReturnDirector> sphandler(init.handler,boost::bind(&ReleaseDirector<AsyncTimerEventReturnDirector>,RR_BOOST_PLACEHOLDERS(_1),init.id));
	return new WallTimer(period,boost::bind(&TimerHandlerFunc,RR_BOOST_PLACEHOLDERS(_1),sphandler),oneshot,node);
}
}
