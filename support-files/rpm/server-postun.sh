if [ $1 -ge 1 ]; then
  if [ -x %{_sysconfdir}/init.d/mysql-erydb ] ; then
    # only restart the server if it was alredy running
    if %{_sysconfdir}/init.d/mysql-erydb status > /dev/null 2>&1; then
      %{_sysconfdir}/init.d/mysql-erydb restart
    fi
  fi
fi

if [ $1 = 0 ] ; then
  if [ -x /usr/bin/systemctl ] ; then
    /usr/bin/systemctl daemon-reload > /dev/null 2>&1
  fi
fi

