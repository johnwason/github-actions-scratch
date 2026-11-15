// Go specific logging helpers. Upstream Logging.i declares
// FileLogRecordHandler without its LogRecordHandler base, so it cannot be
// passed to SetLogRecordHandler from the wrapped API.
%inline %{
namespace rrgo
{
void SetFileLogRecordHandler(const boost::shared_ptr<RobotRaconteur::RobotRaconteurNode>& node,
                             const std::string& filename, bool append)
{
    RR_SHARED_PTR<RobotRaconteur::FileLogRecordHandler> h =
        RR_MAKE_SHARED<RobotRaconteur::FileLogRecordHandler>();
    h->OpenFile(filename, append);
    node->SetLogRecordHandler(h);
}
} // namespace rrgo
%}
