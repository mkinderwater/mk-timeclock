from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parent.parent
class RoleInstall124(unittest.TestCase):
 def test_entrypoints(self):
  for name,flag in [('install-master.sh','--master'),('install-slave.sh','--slave')]:
   self.assertIn(flag,(ROOT/name).read_text())
 def test_master_registration_before_agent(self):
  text=(ROOT/'install.sh').read_text()
  self.assertLess(text.index('timeclock-register-primary.sh; then'),text.index('systemctl enable --now timeclock-agent.service',text.index('timeclock-register-primary.sh; then')))
 def test_slave_does_not_start_backend(self):
  text=(ROOT/'install.sh').read_text()
  slave=text.split('else\n  # No server, mailer, reminders, or backup timer on unadopted Slave.')[1]
  self.assertNotIn('enable --now timeclock-server.service',slave)
  self.assertIn('enable --now mk-timeclock-setup.service',slave)
 def test_no_role_form_or_action(self):
  code=(ROOT/'clock-setup.py').read_text()
  self.assertNotIn('action="/role"',code)
  self.assertNotIn('apply-role.sh',code)
 def test_preflight_rejects_cross_role(self):
  text=(ROOT/'install.sh').read_text()
  self.assertIn('Authority transfer is required',text)
  self.assertIn('Primary database exists. Refusing Slave install',text)
if __name__=='__main__':unittest.main()
