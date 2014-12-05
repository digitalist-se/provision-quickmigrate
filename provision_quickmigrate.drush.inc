<?php
/**
 * @file
 * Main drush command file for quick migrate.
 */

/**
 * Implements hook_drush_command().
 */
function provision_quickmigrate_drush_command() {
  $items = array();

  $items['provision-quickmigrate'] = array(
    'description' => 'Do a quick migration.',
    'arguments' => array(
      '@platform_name' => dt('The Drush alias of the platform.'),
      'new_uri' => dt("The new URI the side should reside on."),
    ),
    'examples' => array(
      'drush @site provision-migrate @platform_name' => 'Migrate the site as defined by the Drush alias, to the platform as defined by the platform\'s Drush alias',
    ),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_ROOT,
  );
  return $items;
}

/**
 * Perform the actual migration.
 */
function drush_provision_quickmigrate($platform, $new_uri = NULL) {
  // Purge our local files folder.
  drush_log("Quickmigrate: purging local files folders");
  _provision_recursive_delete(d()->site_path . '/files');
  _provision_recursive_delete(d()->site_path . '/private/files');

  // Put the site under maintenance
  provision_backend_invoke('@' . d()->uri, 'vset maintenance_mode 1');
  drush_set_option('new_platform', $platform);
  drush_log("Quickmigrate: New platform = $platform");
  // Move the site folder to another location.
  $site_path = d()->site_path;
  $backup_uri = isset($new_uri) ? $new_uri :  d()->uri;
  drush_log("Quickmigrate: Backup uri = $backup_uri");
  // Get the web server we are working with.
  $server = d(d()->platform->web_server);
  $server->shell_exec("mv {$site_path}/files /var/aegir/{$backup_uri}_files");
  $server->shell_exec("mv {$site_path}/private/files /var/aegir/{$backup_uri}_private_files");
 // d()->service('http')->fetch(d()->site_path . '/private/');
  d()->service('http')->fetch(d()->site_path . '/modules/');
  d()->service('http')->fetch(d()->site_path . '/themes/');
  d()->service('http')->fetch(d()->site_path . '/libraries/');
  d()->service('http')->fetch(d()->site_path . '/local.settings.php');

  drush_set_option('old_platform', d()->platform->name);
  drush_log("Quickmigrate: Old platform = " . d()->platform->name);
  // Sync things to the local directory.

  // If we are renaming the site, pick this up early.
  if (!is_null($new_uri) && ($new_uri != d()->uri)) {
    drush_set_option('target_name', $new_uri);
  }
  else {
    $new_uri = d()->uri;
    // the site url stays the same
    drush_set_option('target_name', '@' . d()->uri);
  }
  $target = drush_get_option('target_name');
  drush_log("Quickmigrate: target = $target");
  $new_path = d($platform)->root . "/sites/$new_uri";
  drush_set_option('new_site_path', $new_path);
  drush_log("Quickmigrate: new_site_path = $new_path");
  $old_path = d()->site_path;
  drush_set_option('old_site_path', $old_path);
  drush_log("Quickmigrate: old_site_path = $old_path");
  if (!drush_get_error() && rename($old_path, $new_path)) {
    provision_quickmigrate_save_site(d(), $platform, $target, $new_uri);
    if (!drush_get_error()) {
      provision_backend_invoke($target, 'updatedb');
      provision_backend_invoke($target, 'cc all');
      // @todo This should be moved to hosting_quickmigrate.
      provision_backend_invoke('@hostmaster', 'hosting-import', array($target));
    }
  }
  if (!drush_get_error()) {
    // Verify the platform to get it deployed.
    drush_set_option('provision_save_config', false);
    provision_backend_invoke($target, 'provision-verify');
  }
}

function drush_provision_quickmigrate_pre_provision_quickmigrate() {
  // Generate a database dump.
  d()->service('db')->generate_dump();
}

function drush_provision_quickmigrate_post_provision_quickmigrate(){

  $old_path = drush_get_option('old_site_path');
  $old_platform = drush_get_option('old_platform');
  d($old_platform)->service('http')->sync($old_path);
  $backup_uri = d()->uri;
  $server = d(d()->platform->web_server);
  $group = _get_webserver_group($server);
  drush_log("Quickmigrate: remote webserver group = $group");
  // Replace files directory.
  $site_path = drush_get_option('new_site_path');
  $server->shell_exec("chgrp {$group} {$site_path}/private");
  $server->shell_exec("mkdir {$site_path}/private/temp");
  $server->shell_exec("chgrp {$group} {$site_path}/private/temp");
  $server->shell_exec("chgrp {$group} {$site_path}/settings.php");
  $server->shell_exec("rm -rf {$site_path}/files");
  $server->shell_exec("rm -rf {$site_path}/private/files");
  $server->shell_exec("mv /var/aegir/{$backup_uri}_files {$site_path}/files");
  $server->shell_exec("mv /var/aegir/{$backup_uri}_private_files {$site_path}/private/files");
  $server->shell_exec("rm {$site_path}/database.sql");
  unlink("{$site_path}/database.sql");
  // Put the site out of maintenance
  provision_backend_invoke(drush_get_option('target_name'), 'vset maintenance_mode 0');
}

/**
 * Save a site with provision_save().
 * @param $context
 *   The context to use.
 * @param $target
 *   The target platform.
 */
function provision_quickmigrate_save_site($context, $platform, $target, $new_uri = NULL) {
  $options = array();
  $options['uri'] = $context->uri;
  if (!is_null($new_uri) && ($new_uri != $context->uri)) {
    $options = $context->options;
    $options['uri'] = ltrim($new_uri, '@');
    $hash_name = drush_get_option('#name') ? '#name' : 'name';
    $options[$hash_name] = $new_uri;
  }
  $options['platform'] = $platform;
  $options['root'] = d($platform)->root;
  $options['profile'] = $context->profile;

  if ($db_server = drush_get_option('new_db_server', FALSE)) {
    $options['db_server'] = $db_server;
  }
  drush_invoke_process('@self', 'provision-save', array($target), $options);
}

/**
 * Handle rollback. If we have got to the point that we tried to move the site,
 * we need to move the site back and restore the database dump.
 */
function drush_provision_quickmigrate_rollback() {
  $new_path = drush_get_option('new_site_path');
  $old_path = drush_get_option('old_site_path');
  $new_platform = drush_get_option('new_platform');
  $target = drush_get_option('target_name');
  if (is_dir($new_path)) {
    if (rename($new_path, $old_path)) {
      provision_quickmigrate_save_site(d(), drush_get_option('old_platform'), drush_get_option('target_name'), d()->uri);
      drush_set_option('provision_save_config', false);
      provision_backend_invoke($target, 'provision-verify');
      d()->service('db')->import_site_database();
      // Sync web fronts.
      d($new_platform)->service('http')->sync($new_path);
      // @todo This should be moved to hosting_quickmigrate.
      $backup_uri = d()->uri;
      $server = d(d()->platform->web_server);
      // Replace files directory.
      $server->shell_exec("rm -rf {$old_path}/files");
      $server->shell_exec("mv /var/aegir/{$backup_uri}_files {$old_path}/files");
      $server->shell_exec("mv /var/aegir/{$backup_uri}_private_files {$old_path}/private/files" );
      $server->shell_exec("rm {$old_path}/database.sql");
      // Import changes to the hostmaster.
      provision_backend_invoke('@hostmaster', 'hosting-import', array($target));
    }
  }
  else {
    drush_set_error("Cannot rollback update");
  }
  // Put the site out of maintenance
  provision_backend_invoke(drush_get_option('target_name'), 'vset maintenance_mode 0');
}

function _get_webserver_group($server){
  $server->shell_exec("groups aegir");
  $result = drush_shell_exec_output();
  $groups = explode(' ', $result[0]);
  return $groups[3];
}
